// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_decoder.h"
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
namespace ff {
namespace {
void check(int result, const QString &action) {
    if (result >= 0)
        return;
    char message[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(result, message, sizeof(message));
    throw Error(action + ": " + QString::fromUtf8(message));
}
class Stream {
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *decoded = nullptr, *previous = nullptr, *future = nullptr;
    SwsContext *scale = nullptr;
    SwrContext *resample = nullptr;
    int index = -1;
    bool draining = false, eof = false, audioContinuous = false;
    qint64 originUs = 0, lastVideo = -1, lastAudio = -1;
    qint64 audioStart = 0, audioNext = 0;
    std::vector<float> audioData;
    std::function<bool()> cancelled;
    void cleanup() {
        sws_freeContext(scale);
        swr_free(&resample);
        av_frame_free(&previous);
        av_frame_free(&future);
        av_frame_free(&decoded);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    void ensureActive() const {
        if (cancelled && cancelled())
            throw Error("Playback request cancelled");
    }
    qint64 timestamp(const AVFrame *frame) const {
        auto pts = frame->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE)
            throw Error("Source frame has no presentation timestamp");
        return av_rescale_q(pts, format->streams[index]->time_base, AVRational{1, 1000000}) - originUs;
    }
    void seek(qint64 us) {
        ensureActive();
        const auto stamp = av_rescale_q(std::max(qint64(0), us) + originUs,
                                       AVRational{1, 1000000}, format->streams[index]->time_base);
        check(av_seek_frame(format, index, stamp, AVSEEK_FLAG_BACKWARD), "Seek source");
        avcodec_flush_buffers(codec);
        av_packet_unref(packet);
        av_frame_free(&previous);
        av_frame_free(&future);
        swr_free(&resample);
        audioData.clear();
        audioContinuous = false;
        audioNext = av_rescale_q(us, AVRational{1, 1000000}, AVRational{1, 48000});
        draining = eof = false;
    }
    bool next() {
        while (!eof) {
            ensureActive();
            av_frame_unref(decoded);
            const int received = avcodec_receive_frame(codec, decoded);
            if (received >= 0)
                return true;
            if (received == AVERROR_EOF) {
                eof = true;
                return false;
            }
            if (received != AVERROR(EAGAIN))
                check(received, "Decode source");
            if (draining) {
                eof = true;
                return false;
            }
            for (;;) {
                ensureActive();
                av_packet_unref(packet);
                const int read = av_read_frame(format, packet);
                if (read == AVERROR_EOF) {
                    draining = true;
                    check(avcodec_send_packet(codec, nullptr), "Flush source decoder");
                    break;
                }
                check(read, "Read source");
                if (packet->stream_index == index) {
                    check(avcodec_send_packet(codec, packet), "Submit source packet");
                    break;
                }
            }
        }
        return false;
    }
    bool nextAudio() {
        audioData.clear();
        if (!next()) {
            if (!resample)
                return false;
            const int capacity = int(swr_get_delay(resample, 48000)) + 32;
            audioData.resize(size_t(capacity) * 2);
            auto *out = reinterpret_cast<uint8_t *>(audioData.data());
            const int count = swr_convert(resample, &out, capacity, nullptr, 0);
            check(count, "Drain audio resampler");
            audioData.resize(size_t(count) * 2);
            audioStart = audioNext;
            audioNext += count;
            return count > 0;
        }
        if (!resample) {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            check(swr_alloc_set_opts2(&resample, &stereo, AV_SAMPLE_FMT_FLT, 48000,
                                     &decoded->ch_layout, AVSampleFormat(decoded->format),
                                     decoded->sample_rate, 0, nullptr), "Configure audio resampler");
            check(swr_init(resample), "Start audio resampler");
        }
        const auto delay = swr_get_delay(resample, decoded->sample_rate);
        auto stamp = av_rescale_q(timestamp(decoded), AVRational{1, 1000000}, AVRational{1, 48000}) -
                     av_rescale_rnd(delay, 48000, decoded->sample_rate, AV_ROUND_NEAR_INF);
        const qint64 quantum = av_rescale_q(1, format->streams[index]->time_base, AVRational{1, 48000});
        // Containers such as Matroska quantize timestamps to milliseconds.
        // Continuous sample counts are exact; preserve real gaps larger than
        // the container's timestamp precision.
        if (audioContinuous && std::abs(stamp - audioNext) <= std::max(qint64(2), quantum + 2))
            stamp = audioNext;
        audioStart = stamp;
        audioContinuous = true;
        const int capacity = int(av_rescale_rnd(delay + decoded->nb_samples, 48000,
                                               decoded->sample_rate, AV_ROUND_UP)) + 32;
        audioData.resize(size_t(capacity) * 2);
        auto *out = reinterpret_cast<uint8_t *>(audioData.data());
        const int count = swr_convert(resample, &out, capacity,
                                      const_cast<const uint8_t **>(decoded->extended_data),
                                      decoded->nb_samples);
        check(count, "Resample audio");
        audioData.resize(size_t(count) * 2);
        audioNext = audioStart + count;
        return true;
    }
  public:
    Stream(const QString &path, AVMediaType type, std::function<bool()> cancel) : cancelled(std::move(cancel)) {
        try {
            ensureActive();
            if (!QFileInfo(path).isFile())
                throw Error("Missing source: " + path);
            format = avformat_alloc_context();
            if (!format)
                throw Error("Cannot allocate source decoder");
            format->interrupt_callback = {[](void *opaque) {
                auto *self = static_cast<Stream *>(opaque);
                return self->cancelled && self->cancelled() ? 1 : 0;
            }, this};
            check(avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr),
                  "Open source");
            check(avformat_find_stream_info(format, nullptr), "Inspect source");
            index = av_find_best_stream(format, type, -1, -1, nullptr, 0);
            check(index, "Find source stream");
            originUs = format->start_time == AV_NOPTS_VALUE ? 0 : format->start_time;
            const AVCodec *decoder = avcodec_find_decoder(format->streams[index]->codecpar->codec_id);
            if (!decoder)
                throw Error("No software decoder for source");
            codec = avcodec_alloc_context3(decoder);
            if (!codec)
                throw Error("Cannot allocate codec");
            check(avcodec_parameters_to_context(codec, format->streams[index]->codecpar), "Read codec");
            codec->pkt_timebase = format->streams[index]->time_base;
            codec->thread_count = 1;
            check(avcodec_open2(codec, decoder, nullptr), "Start source decoder");
            decoded = av_frame_alloc();
            packet = av_packet_alloc();
            if (!decoded || !packet)
                throw Error("Cannot allocate source buffers");
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~Stream() { cleanup(); }
    QImage video(qint64 us, QSize size) {
        if (lastVideo < 0 || us < lastVideo || us - lastVideo > 1000000)
            seek(us);
        lastVideo = us;
        while (true) {
            if (!future) {
                if (!next())
                    break;
                future = av_frame_clone(decoded);
                if (!future)
                    throw Error("Cannot retain decoded frame");
            }
            if (timestamp(future) > us + 2)
                break;
            av_frame_free(&previous);
            previous = future;
            future = nullptr;
        }
        if (!previous)
            return {};
        const auto sar = av_guess_sample_aspect_ratio(format, format->streams[index], previous);
        const double aspect = double(previous->width) / previous->height * (sar.num > 0 ? av_q2d(sar) : 1);
        QSize fit(size.width(), std::max(1, int(std::round(size.width() / aspect))));
        if (fit.height() > size.height())
            fit = {std::max(1, int(std::round(size.height() * aspect))), size.height()};
        QImage image(fit, QImage::Format_RGBA8888);
        scale = sws_getCachedContext(scale, previous->width, previous->height,
                                    AVPixelFormat(previous->format), fit.width(), fit.height(),
                                    AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale || image.isNull())
            throw Error("Cannot convert source frame");
        uint8_t *dest[] = {image.bits(), nullptr, nullptr, nullptr};
        int strides[] = {int(image.bytesPerLine()), 0, 0, 0};
        sws_scale(scale, previous->data, previous->linesize, 0, previous->height, dest, strides);
        return image;
    }
    void mix(qint64 start, int count, float *output) {
        if (lastAudio < 0 || start < lastAudio || start - lastAudio > 48000)
            seek(std::max(qint64(0), (start - 96000) * 1000000 / 48000));
        lastAudio = start;
        const auto end = start + count;
        while (true) {
            ensureActive();
            const auto bufferedEnd = audioStart + qint64(audioData.size() / 2);
            if (audioData.empty() || bufferedEnd <= start) {
                if (!nextAudio())
                    break;
                continue;
            }
            if (audioStart >= end)
                break;
            const auto begin = std::max(start, audioStart), finish = std::min(end, bufferedEnd);
            for (qint64 sample = begin; sample < finish; ++sample)
                for (int channel = 0; channel < 2; ++channel)
                    output[(sample - start) * 2 + channel] +=
                        audioData[size_t((sample - audioStart) * 2 + channel)];
            if (bufferedEnd >= end)
                break;
            audioData.clear();
        }
    }
};
} // namespace
struct TimelineDecoder::Impl {
    Project project;
    QSize size;
    std::function<bool()> cancelled;
    std::map<QString, std::unique_ptr<Stream>> videos, audios;
    Stream &stream(std::map<QString, std::unique_ptr<Stream>> &cache, const Clip &clip, AVMediaType type) {
        auto &value = cache[clip.id];
        if (!value) {
            // A split is a metadata boundary, not a decoder boundary. Retain
            // resampler history and exact sample counts across adjacent cuts.
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (!it->second || it->first == clip.id)
                    continue;
                const auto &before = project.clip(it->first);
                if (before.media == clip.media && before.track == clip.track &&
                    before.start + before.duration == clip.start && before.in + before.duration == clip.in) {
                    value = std::move(it->second);
                    cache.erase(it);
                    break;
                }
            }
        }
        if (!value)
            value = std::make_unique<Stream>(project.source(clip.media).path, type, cancelled);
        return *value;
    }
    void checkCancelled() {
        if (cancelled && cancelled())
            throw Error("Playback request cancelled");
    }
    qint64 sample(qint64 frame) const {
        return frame * 48000LL * project.rate.den / project.rate.num;
    }
};
TimelineDecoder::TimelineDecoder(Project project, QSize size, std::function<bool()> cancelled)
    : impl(std::make_unique<Impl>()) {
    project.validate();
    impl->size = QSize(project.width, project.height).scaled(size.boundedTo({1920, 1080}).expandedTo({16, 16}),
                                                           Qt::KeepAspectRatio);
    impl->project = std::move(project);
    std::stable_sort(impl->project.clips.begin(), impl->project.clips.end(), [](const Clip &a, const Clip &b) {
        return a.track == b.track ? a.start < b.start : a.track < b.track;
    });
    impl->cancelled = std::move(cancelled);
}
TimelineDecoder::~TimelineDecoder() = default;
QImage TimelineDecoder::video(qint64 frame) {
    auto &d = *impl;
    d.checkCancelled();
    QImage image(d.size, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    // Every video layer is opaque and letterboxed, as in the export graph.
    const Clip *top = nullptr;
    for (const auto &clip : d.project.clips)
        if (d.project.tracks[clip.track].kind == "video" && d.project.audibleTrack(clip.track) &&
            frame >= clip.start && frame < clip.start + clip.duration && (!top || clip.track > top->track))
            top = &clip;
    if (top) {
        auto source = d.stream(d.videos, *top, AVMEDIA_TYPE_VIDEO)
                          .video(d.project.rate.us(top->in + frame - top->start), d.size);
        if (!source.isNull()) {
            QPainter painter(&image);
            painter.drawImage((image.width() - source.width()) / 2,
                              (image.height() - source.height()) / 2, source);
        }
    }
    for (auto it = d.videos.begin(); it != d.videos.end();)
        it = !top || it->first != top->id ? d.videos.erase(it) : std::next(it);
    return image;
}
QByteArray TimelineDecoder::audio(qint64 startSample, int samples) {
    auto &d = *impl;
    d.checkCancelled();
    if (samples < 0 || samples > 12000)
        throw Error("Audio request exceeds 250 ms playback buffer");
    std::vector<float> mix(size_t(samples) * 2, 0);
    QSet<QString> active;
    for (const auto &clip : d.project.clips) {
        if (!d.project.audibleTrack(clip.track) || !d.project.source(clip.media).audio)
            continue;
        const auto begin = std::max(startSample, d.sample(clip.start));
        const auto end = std::min(startSample + samples, d.sample(clip.start + clip.duration));
        if (begin >= end)
            continue;
        active.insert(clip.id);
        d.stream(d.audios, clip, AVMEDIA_TYPE_AUDIO)
            // Round the source/timeline offset once. Rounding both endpoints
            // separately changes phase by a sample when splitting 29.97 fps.
            .mix(begin + d.sample(clip.in - clip.start), int(end - begin),
                 mix.data() + (begin - startSample) * 2);
    }
    for (auto it = d.audios.begin(); it != d.audios.end();)
        it = !active.contains(it->first) ? d.audios.erase(it) : std::next(it);
    // Shared with the explicit export filter: unity gain, bounded peak.
    for (float &sample : mix)
        sample = std::clamp(sample, -0.95f, 0.95f);
    return QByteArray(reinterpret_cast<const char *>(mix.data()), qsizetype(mix.size() * sizeof(float)));
}
} // namespace ff
