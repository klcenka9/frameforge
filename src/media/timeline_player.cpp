// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_player.h"
#include "timeline_decoder.h"
#include <QAudioSink>
#include <QElapsedTimer>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
namespace ff {
namespace {
constexpr int sampleRate = 48000, blockSamples = 1920; // 40 ms, never a whole timeline.
struct Picture {
    qint64 frame;
    QImage image;
};
struct Request {
    Project project;
    QSize size;
    quint64 generation;
    qint64 sample, end;
    bool still;
};
struct Result {
    quint64 generation;
    qint64 end;
    bool still;
    std::vector<Picture> pictures;
    QByteArray pcm;
    QString error;
};
qint64 sampleAt(const Rate &rate, qint64 frame) {
    return frame * sampleRate * qint64(rate.den) / rate.num;
}
qint64 frameAt(const Rate &rate, qint64 sample) {
    auto frame = sample * rate.num / (sampleRate * qint64(rate.den));
    // A rational frame boundary can fall between samples. Match sampleAt's
    // rounding rather than mapping the first sample to the preceding frame.
    if (sampleAt(rate, frame + 1) <= sample)
        ++frame;
    return frame;
}
} // namespace
struct TimelinePlayer::Impl {
    TimelinePlayer *owner;
    Project project;
    QSize size{960, 540};
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    std::unique_ptr<QAudioSink> sink;
    QIODevice *audioPort = nullptr;
    bool integerAudio = false, playing = false, clockStarted = false, outstanding = false, buffering = false;
    qint64 currentFrame = 0, displayedFrame = -1, originSample = 0, nextSample = 0, bufferedEnd = 0;
    QByteArray pcm;
    std::deque<Picture> pictures;
    QElapsedTimer clock;
    QElapsedTimer audioProgress;
    qint64 lastClockSample = 0;
    QTimer timer;
    std::atomic<quint64> generation{0};
    std::atomic_bool shutdown{false};
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Request> request;
    std::thread worker;

    explicit Impl(TimelinePlayer *q) : owner(q) {
        timer.setInterval(8);
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, owner, [this] { tick(); });
        worker = std::thread([this] { work(); });
    }
    ~Impl() {
        timer.stop();
        shutdown = true;
        ++generation;
        wake.notify_one();
        worker.join();
        stopAudio();
    }
    void stopAudio() {
        audioPort = nullptr;
        if (sink) {
            QObject::disconnect(sink.get(), nullptr, owner, nullptr);
            // reset discards queued samples. stop may drain a suspended
            // backend and block the GUI during pause/seek/device fallback.
            sink->reset();
            sink.reset();
        }
    }
    void setBuffering(bool value) {
        if (value != buffering) {
            buffering = value;
            emit owner->bufferingChanged(value);
        }
    }
    void silentFallback(const QString &reason) {
        // A stopped backend may already have reset processedUSecs(). Preserve
        // the last observed sample instead of reading its reset clock again.
        const auto at = lastClockSample;
        stopAudio();
        originSample = at;
        clock.restart();
        pcm.clear(); // Never replay obsolete audio if the device recovers.
        setBuffering(false);
        emit owner->error(reason + "; continuing silent preview. Pause/Play retries audio.");
    }
    void clear() {
        ++generation;
        {
            std::lock_guard lock(mutex);
            request.reset();
        }
        outstanding = false;
        timer.stop();
        stopAudio();
        pcm.clear();
        pictures.clear();
        clockStarted = false;
        setBuffering(false);
        displayedFrame = -1;
        originSample = nextSample = bufferedEnd = sampleAt(project.rate, currentFrame);
        lastClockSample = originSample;
    }
    void submit(bool still) {
        if (outstanding || shutdown)
            return;
        const auto end =
            still ? nextSample : std::min(sampleAt(project.rate, project.end()), nextSample + blockSamples);
        if (!still && end <= nextSample)
            return;
        Request next{project, size, generation.load(), nextSample, end, still};
        outstanding = true;
        {
            std::lock_guard lock(mutex);
            request = std::move(next);
        }
        wake.notify_one();
    }
    void work() {
        std::unique_ptr<TimelineDecoder> decoder;
        quint64 decoderGeneration = 0;
        while (!shutdown) {
            std::optional<Request> next;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return shutdown || request.has_value(); });
                if (shutdown)
                    break;
                next = std::move(request);
                request.reset();
            }
            const auto &r = *next;
            if (r.generation != generation)
                continue;
            Result result{r.generation, r.end, r.still, {}, {}, {}};
            try {
                if (!decoder || decoderGeneration != r.generation) {
                    decoder =
                        std::make_unique<TimelineDecoder>(r.project, r.size, [this, token = r.generation] {
                            return shutdown || generation != token;
                        });
                    decoderGeneration = r.generation;
                }
                const auto first = frameAt(r.project.rate, r.sample);
                for (auto frame = first; r.still || sampleAt(r.project.rate, frame) < r.end; ++frame) {
                    result.pictures.push_back({frame, decoder->video(frame)});
                    if (r.still)
                        break;
                }
                if (!r.still)
                    result.pcm = decoder->audio(r.sample, int(r.end - r.sample));
            } catch (const std::exception &e) {
                result.error = QString::fromUtf8(e.what());
                decoder.reset();
            }
            if (shutdown || r.generation != generation)
                continue;
            QMetaObject::invokeMethod(
                owner, [this, result = std::move(result)]() mutable { deliver(std::move(result)); },
                Qt::QueuedConnection);
        }
    }
    void startAudio() {
        if (device.isNull())
            return;
        QAudioFormat format;
        format.setSampleRate(sampleRate);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Float);
        integerAudio = !device.isFormatSupported(format);
        if (integerAudio)
            format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format)) {
            emit owner->error("Audio output does not support 48 kHz stereo; preview is silent");
            return;
        }
        sink = std::make_unique<QAudioSink>(device, format);
        sink->setBufferSize(format.bytesForFrames(blockSamples * 2));
        sink->setVolume(0.7);
        audioPort = sink->start();
        if (!audioPort || sink->error() != QAudio::NoError) {
            stopAudio();
            emit owner->error("Audio output unavailable; using silent preview clock");
            return;
        }
        QObject::connect(sink.get(), &QAudioSink::stateChanged, owner, [this](QAudio::State state) {
            if (sink && state == QAudio::StoppedState && sink->error() != QAudio::NoError) {
                // Defer destruction until after the backend's signal returns.
                QTimer::singleShot(0, owner, [this, token = generation.load()] {
                    if (token != generation || !sink)
                        return;
                    silentFallback("Audio device disconnected");
                });
            }
        });
    }
    qint64 positionSample() const {
        if (!clockStarted)
            return originSample;
        if (sink)
            return originSample + sink->processedUSecs() * sampleRate / 1000000;
        return originSample + clock.nsecsElapsed() * sampleRate / 1000000000;
    }
    void deliver(Result result) {
        if (result.generation != generation)
            return;
        outstanding = false;
        if (!result.error.isEmpty()) {
            playing = false;
            clear();
            emit owner->playingChanged(false);
            emit owner->error(result.error);
            return;
        }
        if (result.still) {
            setBuffering(false);
            if (!result.pictures.empty())
                emit owner->frameReady(result.pictures.front().image);
            return;
        }
        nextSample = bufferedEnd = result.end;
        for (auto &picture : result.pictures)
            if (pictures.empty() || pictures.back().frame < picture.frame)
                pictures.push_back(std::move(picture));
        if (!clockStarted) {
            startAudio();
            clock.restart();
            audioProgress.start();
            clockStarted = true;
            timer.start();
        }
        if (sink) {
            if (integerAudio) {
                const auto oldSize = pcm.size();
                pcm.resize(oldSize + result.pcm.size() / 2);
                for (qsizetype i = 0; i < result.pcm.size() / 4; ++i) {
                    float sample;
                    std::memcpy(&sample, result.pcm.constData() + i * 4, 4);
                    const auto value = qint16(std::lround(std::clamp(sample, -1.f, 1.f) * 32767));
                    std::memcpy(pcm.data() + oldSize + i * 2, &value, 2);
                }
            } else
                pcm += result.pcm;
        }
        tick();
    }
    void tick() {
        if (!playing || !clockStarted)
            return;
        if (sink && audioPort && !pcm.isEmpty()) {
            const auto count = std::min(qint64(pcm.size()), qint64(sink->bytesFree()));
            if (count > 0) {
                const auto written = audioPort->write(pcm.constData(), count);
                if (written > 0)
                    pcm.remove(0, written);
            }
        }
        const auto end = sampleAt(project.rate, project.end());
        auto now = std::min(std::max(positionSample(), lastClockSample), end);
        if (sink) {
            if (now > lastClockSample)
                audioProgress.restart();
            else if (audioProgress.elapsed() > 3000) {
                silentFallback("Audio clock stalled");
                now = std::min(positionSample(), end);
            }
        }
        lastClockSample = now;
        setBuffering(sink ? sink->state() == QAudio::IdleState : bufferedEnd <= now && nextSample < end);
        const auto frame = std::min(frameAt(project.rate, now), project.end());
        // Present the latest available frame; late display frames are discarded.
        std::optional<Picture> shown;
        while (!pictures.empty() && pictures.front().frame <= frame) {
            shown = std::move(pictures.front());
            pictures.pop_front();
        }
        if (shown && shown->frame != displayedFrame) {
            setBuffering(false);
            displayedFrame = shown->frame;
            emit owner->frameReady(shown->image);
        }
        if (frame != currentFrame) {
            currentFrame = frame;
            emit owner->positionChanged(currentFrame);
        }
        if (now >= end - 48 && nextSample >= end && pcm.isEmpty()) {
            currentFrame = project.end();
            playing = false;
            clear();
            emit owner->positionChanged(currentFrame);
            emit owner->playingChanged(false);
            emit owner->ended();
            return;
        }
        // Sink (80 ms) + queued output (up to 120 ms) stays below 250 ms.
        if (!outstanding && nextSample - now < blockSamples * 2)
            submit(false);
    }
};
TimelinePlayer::TimelinePlayer(QObject *parent) : QObject(parent), impl(std::make_unique<Impl>(this)) {}
TimelinePlayer::~TimelinePlayer() = default;
void TimelinePlayer::setProject(const Project &project) {
    project.validate();
    impl->playing = false;
    impl->clear();
    impl->project = project;
    impl->currentFrame = std::min(impl->currentFrame, project.end());
    seek(impl->currentFrame);
    emit playingChanged(false);
}
void TimelinePlayer::setAudioDevice(const QAudioDevice &device) {
    pause();
    impl->device = device;
}
void TimelinePlayer::setPreviewSize(QSize size) {
    size = size.boundedTo({1920, 1080}).expandedTo({16, 16});
    if (size == impl->size)
        return;
    const bool resume = isPlaying();
    pause();
    impl->size = size;
    seek(frame());
    if (resume)
        play();
}
void TimelinePlayer::play() {
    if (impl->playing)
        return;
    if (impl->project.clips.isEmpty())
        return;
    if (impl->currentFrame >= impl->project.end())
        impl->currentFrame = 0;
    impl->clear();
    impl->playing = true;
    impl->setBuffering(true);
    emit playingChanged(true);
    impl->submit(false);
}
void TimelinePlayer::pause() {
    if (!impl->playing)
        return;
    impl->tick();
    impl->playing = false;
    impl->clear();
    emit playingChanged(false);
}
void TimelinePlayer::seek(qint64 frame) {
    impl->playing = false;
    impl->currentFrame = std::clamp(frame, qint64(0), impl->project.end());
    impl->clear();
    emit positionChanged(impl->currentFrame);
    emit playingChanged(false);
    impl->setBuffering(true);
    impl->submit(true);
}
qint64 TimelinePlayer::frame() const {
    return impl->currentFrame;
}
bool TimelinePlayer::isPlaying() const {
    return impl->playing;
}
} // namespace ff
