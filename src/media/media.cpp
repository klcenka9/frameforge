// SPDX-License-Identifier: GPL-3.0-or-later
#include "media.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <cmath>
namespace ff {
static QByteArray run(const QString &tool, const QStringList &args, int timeout = 20000) {
    QProcess p;
    p.start(tool, args);
    if (!p.waitForStarted(3000))
        throw Error(tool + " is not available: " + p.errorString());
    if (!p.waitForFinished(timeout)) {
        p.kill();
        p.waitForFinished();
        throw Error(tool + " timed out");
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        throw Error(tool + ": " + QString::fromUtf8(p.readAllStandardError()).right(1800));
    return p.readAllStandardOutput();
}
Media probe(const QString &path) {
    if (!QFileInfo(path).isFile())
        throw Error("Media file is missing: " + path);
    auto bytes = run("ffprobe", {"-v", "error", "-show_streams", "-show_format", "-of", "json",
                                 QFileInfo(path).absoluteFilePath()});
    QJsonParseError e;
    auto o = QJsonDocument::fromJson(bytes, &e).object();
    if (e.error != QJsonParseError::NoError)
        throw Error("Invalid FFprobe response");
    Media m;
    m.id = uuid();
    m.path = QFileInfo(path).absoluteFilePath();
    double duration = o["format"].toObject()["duration"].toString().toDouble();
    for (auto v : o["streams"].toArray()) {
        auto s = v.toObject();
        QString type = s["codec_type"].toString();
        if (type == "video" && !s["disposition"].toObject()["attached_pic"].toInt()) {
            m.video = true;
            m.width = s["width"].toInt();
            m.height = s["height"].toInt();
        }
        if (type == "audio")
            m.audio = true;
        duration = std::max(duration, s["duration"].toString().toDouble());
    }
    if (!std::isfinite(duration) || duration <= 0 || duration > 40000000 || (!m.video && !m.audio))
        throw Error("Unsupported media or no finite duration");
    m.durationUs = qint64(duration * 1000000);
    return m;
}
QImage thumbnail(const QString &path) {
    try {
        return QImage::fromData(run("ffmpeg", {"-v", "error", "-i", path, "-frames:v", "1", "-vf",
                                               "scale=192:108:force_original_aspect_ratio=decrease", "-f",
                                               "image2pipe", "-c:v", "png", "-threads", "1", "pipe:1"}));
    } catch (const Error &) {
        return {};
    }
}
QStringList encoders() {
    QStringList list;
    auto text = QString::fromUtf8(run("ffmpeg", {"-hide_banner", "-encoders"}));
    for (const auto &line : text.split('\n')) {
        auto fields = line.simplified().split(' ');
        if (fields.size() > 1 && fields[0].size() == 6 && (fields[0][0] == 'V' || fields[0][0] == 'A'))
            list << fields[1];
    }
    return list;
}
QString diagnostics() {
    QString text = "Processing: CPU/software\n";
    try {
        auto available = encoders();
        for (const auto &c : available)
            if (c.contains("nvenc") || c.contains("vaapi") || c.contains("qsv"))
                text += "Compiled encoder (device NOT verified): " + c + "\n";
    } catch (const Error &e) {
        text += QString::fromUtf8(e.what());
    }
    QDir dri("/dev/dri");
    text += "DRM device nodes: " + dri.entryList({"renderD*"}, QDir::System).join(", ") + "\n";
    text += "NVIDIA device node: " + QString(QFileInfo::exists("/dev/nvidia0") ? "present" : "absent") +
            "\nOnly CPU rendering is exposed in this release. No CUDA requirement.";
    return text;
}
} // namespace ff
