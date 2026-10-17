// SPDX-License-Identifier: GPL-3.0-or-later
#include "render.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <algorithm>
namespace ff {
static QString sec(const Project &p, qint64 f) {
    return QString::number(double(f) * p.rate.den / p.rate.num, 'f', 9);
}
QStringList renderArguments(const Project &p, const QString &out, const QString &encoder) {
    p.validate();
    if (p.clips.isEmpty())
        throw Error("Timeline is empty");
    if (p.clips.size() > 64)
        throw Error("This render engine supports up to 64 clips per render");
    if (encoder != "mpeg4" && encoder != "libx264")
        throw Error("Unsupported software encoder");
    QStringList args{"-hide_banner", "-nostdin", "-v", "warning", "-n", "-filter_complex_threads", "1"};
    QStringList filters;
    const QString duration = sec(p, p.end());
    filters << QString("color=c=black:s=%1x%2:r=%3:d=%4,format=yuv420p[base]")
                   .arg(p.width)
                   .arg(p.height)
                   .arg(p.rate.text(), duration);
    filters << QString("anullsrc=r=48000:cl=stereo,atrim=duration=%1[silence]").arg(duration);
    QVector<Clip> clips = p.clips;
    std::stable_sort(clips.begin(), clips.end(), [](const Clip &a, const Clip &b) {
        return a.track == b.track ? a.start < b.start : a.track < b.track;
    });
    QString video = "base";
    QStringList audios{"[silence]"};
    int index = 0;
    for (const auto &c : clips) {
        if (!p.audibleTrack(c.track))
            continue;
        const auto &m = p.source(c.media);
        if (!QFileInfo(m.path).isFile())
            throw Error("Missing media: " + m.path);
        args << "-threads" << "1" << "-i" << m.path;
        auto tag = QString::number(index);
        if (p.tracks[c.track].kind == "video") {
            // Sample onto the project grid first, holding the last preceding
            // source frame. Subtract the nominal in-point, not the first frame
            // retained by trim (which could have a later presentation time).
            filters << QString("[%1:v:0]fps=%4:round=up,trim=start=%2:duration=%3,setpts=PTS-%8,scale=%5:%6:"
                               "force_original_aspect_ratio=decrease,pad=%5:%6:(ow-iw)/2:(oh-ih)/"
                               "2,setsar=1,format=yuv420p,setpts=PTS+%7/TB[v%1]")
                           .arg(tag, sec(p, c.in), sec(p, c.duration), p.rate.text())
                           .arg(p.width)
                           .arg(p.height)
                           .arg(sec(p, c.start))
                           .arg(c.in);
            auto next = "mix" + tag;
            filters << QString(
                           "[%1][v%2]overlay=eof_action=pass:repeatlast=0:enable='gte(t,%3)*lt(t,%4)'[%5]")
                           .arg(video, tag, sec(p, c.start), sec(p, c.start + c.duration), next);
            video = next;
        }
        if (m.audio) {
            filters << QString("[%1:a:0]atrim=start=%2:duration=%3,asetpts=PTS-%2/TB,aresample=48000,"
                               "aformat=channel_layouts=stereo,adelay=%4S:all=1[a%1]")
                           .arg(tag, sec(p, c.in), sec(p, c.duration))
                           .arg(c.start * 48000LL * p.rate.den / p.rate.num);
            audios << "[a" + tag + "]";
        }
        ++index;
    }
    filters << QString("%1amix=inputs=%2:duration=longest:normalize=0,"
                       "aeval='clip(val(0),-0.95,0.95)|clip(val(1),-0.95,0.95)',"
                       "aformat=channel_layouts=stereo[aout]")
                   .arg(audios.join(""))
                   .arg(audios.size());
    args << "-filter_complex" << filters.join(';') << "-map" << "[" + video + "]" << "-map" << "[aout]"
         << "-t" << duration << "-c:v" << encoder;
    if (encoder == "libx264")
        args << "-preset" << "veryfast" << "-crf" << "20";
    else
        args << "-q:v" << "3";
    args << "-pix_fmt" << "yuv420p" << "-threads" << "2" << "-c:a" << "aac" << "-b:a" << "192k" << "-movflags"
         << "+faststart" << "-progress" << "pipe:1" << "-f" << "mp4" << out;
    return args;
}
RenderJob::RenderJob(QObject *parent) : QObject(parent) {
    connect(&process, &QProcess::readyReadStandardError, this, [this] {
        errors += QString::fromUtf8(process.readAllStandardError());
        if (errors.size() > 12000)
            errors = errors.right(12000);
    });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        pending += process.readAllStandardOutput();
        if (validating)
            return;
        while (pending.contains('\n')) {
            auto line = pending.left(pending.indexOf('\n'));
            pending.remove(0, pending.indexOf('\n') + 1);
            if (line.startsWith("out_time_us=")) {
                auto time = line.mid(12).toLongLong();
                emit progress(
                    int(std::clamp(time * 100 / std::max(qint64(1), durationUs), qint64(0), qint64(99))));
            }
        }
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (active && e == QProcess::FailedToStart)
            complete(false, process.errorString());
    });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (!active)
                    return;
                if (cancelled) {
                    complete(false, "Render cancelled");
                    return;
                }
                if (code || status != QProcess::NormalExit) {
                    complete(false, "Render failed: " + errors.right(3000));
                    return;
                }
                if (!validating) {
                    validating = true;
                    pending.clear();
                    errors.clear();
                    process.start("ffprobe",
                                  {"-v", "error", "-show_format", "-show_streams", "-of", "json", temp});
                    return;
                }
                pending += process.readAllStandardOutput();
                auto o = QJsonDocument::fromJson(pending).object();
                bool video = false, audio = false;
                for (auto v : o["streams"].toArray()) {
                    auto s = v.toObject();
                    video |= s["codec_type"] == "video";
                    audio |= s["codec_type"] == "audio";
                }
                double duration = o["format"].toObject()["duration"].toString().toDouble();
                if (!video || !audio ||
                    qAbs(duration * 1000000 - durationUs) > std::max(200000.0, durationUs * 0.01)) {
                    complete(false, "Output validation failed: missing streams or duration mismatch");
                    return;
                }
                if (QFileInfo::exists(target) || !QFile::rename(temp, target)) {
                    complete(false, "Could not publish output; destination may already exist");
                    return;
                }
                complete(true, target);
            });
}
RenderJob::~RenderJob() {
    disconnect(&process, nullptr, this, nullptr);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    if (!temp.isEmpty())
        QFile::remove(temp);
}
void RenderJob::start(const Project &p, const QString &output, const QString &encoder) {
    if (active)
        throw Error("A render is already running");
    if (QFileInfo::exists(output))
        throw Error("Output already exists; choose a new filename");
    for (const auto &m : p.media)
        if (QFileInfo(m.path).absoluteFilePath() == QFileInfo(output).absoluteFilePath())
            throw Error("Output cannot replace source footage");
    target = QFileInfo(output).absoluteFilePath();
    temp = target + "." + uuid() + ".part.mp4";
    auto args = renderArguments(p, temp, encoder);
    durationUs = p.rate.us(p.end());
    errors.clear();
    pending.clear();
    cancelled = false;
    validating = false;
    active = true;
    process.start("ffmpeg", args);
}
void RenderJob::cancel() {
    if (active) {
        cancelled = true;
        process.kill();
    }
}
void RenderJob::complete(bool success, const QString &message) {
    if (!active)
        return;
    active = false;
    if (!success)
        QFile::remove(temp);
    temp.clear();
    emit progress(success ? 100 : 0);
    emit finished(success, message);
}
} // namespace ff
