// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
namespace ff {
static constexpr qint64 maxFrame = 1000000000;
QString uuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
qint64 Rate::us(qint64 f) const {
    return (f / num) * 1000000LL * den + (f % num) * 1000000LL * den / num;
}
qint64 Rate::frames(qint64 t) const {
    const auto unit = 1000000LL * den;
    return (t / unit) * num + (t % unit) * num / unit;
}
QString Rate::text() const {
    return QString::number(num) + "/" + QString::number(den);
}
const Media &Project::source(const QString &id) const {
    for (const auto &m : media)
        if (m.id == id)
            return m;
    throw Error("Unknown media reference");
}
const Clip &Project::clip(const QString &id) const {
    for (const auto &c : clips)
        if (c.id == id)
            return c;
    throw Error("Select a timeline clip first");
}
Clip &Project::clip(const QString &id) {
    for (auto &c : clips)
        if (c.id == id)
            return c;
    throw Error("Select a timeline clip first");
}
qint64 Project::end() const {
    qint64 e = 0;
    for (const auto &c : clips)
        e = std::max(e, c.start + c.duration);
    return e;
}
bool Project::audibleTrack(int i) const {
    bool solo = false;
    for (const auto &t : tracks)
        solo |= t.solo;
    return !tracks[i].muted && (!solo || tracks[i].solo);
}
void Project::validate() const {
    if (rate.num < 1 || rate.num > 240000 || rate.den < 1 || rate.den > 10000 ||
        double(rate.num) / rate.den < 1 || double(rate.num) / rate.den > 240)
        throw Error("Invalid frame rate (1–240 fps required)");
    if (width < 16 || height < 16 || width > 7680 || height > 4320 || width % 2 || height % 2)
        throw Error("Resolution must be even and between 16×16 and 7680×4320");
    if (tracks.isEmpty() || tracks.size() > 32 || media.size() > 10000 || clips.size() > 10000)
        throw Error("Project exceeds supported size");
    QSet<QString> ids;
    for (const auto &m : media) {
        if (m.id.isEmpty() || ids.contains(m.id) || m.path.isEmpty() || m.durationUs <= 0 ||
            m.durationUs > 40000000000000LL || (!m.video && !m.audio))
            throw Error("Invalid or duplicate media");
        ids.insert(m.id);
    }
    for (const auto &t : tracks)
        if (t.kind != "video" && t.kind != "audio")
            throw Error("Unknown track type");
    ids.clear();
    for (const auto &c : clips) {
        if (c.id.isEmpty() || ids.contains(c.id) || c.track < 0 || c.track >= tracks.size() || c.start < 0 ||
            c.in < 0 || c.duration < 1 || c.start > maxFrame || c.in > maxFrame || c.duration > maxFrame ||
            c.start + c.duration > maxFrame)
            throw Error("Invalid clip bounds");
        ids.insert(c.id);
        const auto &m = source(c.media);
        if (c.in + c.duration > rate.frames(m.durationUs))
            throw Error("Trim exceeds source duration");
        if (tracks[c.track].kind == "video" && !m.video)
            throw Error("Audio-only media requires an audio track");
        if (tracks[c.track].kind == "audio" && !m.audio)
            throw Error("This source has no audio");
    }
    QVector<Clip> sorted = clips;
    std::sort(sorted.begin(), sorted.end(), [](const Clip &a, const Clip &b) {
        return a.track == b.track ? a.start < b.start : a.track < b.track;
    });
    for (int i = 1; i < sorted.size(); ++i)
        if (sorted[i].track == sorted[i - 1].track &&
            sorted[i].start < sorted[i - 1].start + sorted[i - 1].duration)
            throw Error("Clips may not overlap within a track; choose another track");
}
QString Project::split(const QString &id, qint64 frame) {
    auto c = clip(id);
    if (tracks[c.track].locked)
        throw Error("Track is locked");
    if (frame <= c.start || frame >= c.start + c.duration)
        throw Error("Place playhead inside the selected clip");
    auto right = c;
    right.id = uuid();
    right.start = frame;
    right.in += frame - c.start;
    right.duration -= frame - c.start;
    clip(id).duration = frame - c.start;
    clips.append(right);
    return right.id;
}
void Project::move(const QString &id, int track, qint64 frame) {
    auto before = *this;
    auto &c = clip(id);
    if (track < 0 || track >= tracks.size() || tracks[c.track].locked || tracks[track].locked)
        throw Error("Invalid or locked track");
    c.track = track;
    c.start = frame;
    try {
        validate();
    } catch (...) {
        *this = before;
        throw;
    }
}
void Project::trim(const QString &id, qint64 in, qint64 duration) {
    auto before = *this;
    auto &c = clip(id);
    if (tracks[c.track].locked)
        throw Error("Track is locked");
    c.in = in;
    c.duration = duration;
    try {
        validate();
    } catch (...) {
        *this = before;
        throw;
    }
}
void Project::erase(const QString &id) {
    auto c = clip(id);
    if (tracks[c.track].locked)
        throw Error("Track is locked");
    clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const Clip &c) { return c.id == id; }),
                clips.end());
}
QJsonObject Project::json(const QString &directory) const {
    QJsonArray ms, ts, cs;
    for (const auto &m : media)
        ms.append(QJsonObject{{"id", m.id},
                              {"path", QDir(directory).relativeFilePath(m.path)},
                              {"durationUs", QString::number(m.durationUs)},
                              {"width", m.width},
                              {"height", m.height},
                              {"video", m.video},
                              {"audio", m.audio}});
    for (const auto &t : tracks)
        ts.append(QJsonObject{
            {"name", t.name}, {"kind", t.kind}, {"muted", t.muted}, {"solo", t.solo}, {"locked", t.locked}});
    for (const auto &c : clips)
        cs.append(QJsonObject{{"id", c.id},
                              {"media", c.media},
                              {"track", c.track},
                              {"start", QString::number(c.start)},
                              {"in", QString::number(c.in)},
                              {"duration", QString::number(c.duration)}});
    return {{"format", "frameforge"},
            {"version", 1},
            {"rate", QJsonObject{{"num", rate.num}, {"den", rate.den}}},
            {"width", width},
            {"height", height},
            {"media", ms},
            {"tracks", ts},
            {"clips", cs}};
}
void Project::save(const QString &path) const {
    validate();
    for (const auto &m : media) {
        const QFileInfo src(m.path), dst(path);
        if (src.absoluteFilePath() == dst.absoluteFilePath() ||
            (!src.canonicalFilePath().isEmpty() && src.canonicalFilePath() == dst.canonicalFilePath()))
            throw Error("Project cannot overwrite source media");
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throw Error(f.errorString());
    auto bytes = QJsonDocument(json(QFileInfo(path).absolutePath())).toJson();
    if (f.write(bytes) != bytes.size() || !f.commit())
        throw Error("Could not safely save project: " + f.errorString());
}
static qint64 integer(const QJsonValue &v) {
    bool ok = false;
    auto n = v.toString().toLongLong(&ok);
    if (!ok)
        throw Error("Expected integer string in project");
    return n;
}
Project Project::load(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw Error(f.errorString());
    if (f.size() > 32 * 1024 * 1024)
        throw Error("Project file is too large");
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    auto o = doc.object();
    if (err.error != QJsonParseError::NoError || o["format"] != "frameforge" || o["version"].toInt() != 1)
        throw Error("Unsupported or corrupt project (schema 1 required)");
    Project p;
    p.rate = {o["rate"].toObject()["num"].toInt(), o["rate"].toObject()["den"].toInt()};
    p.width = o["width"].toInt();
    p.height = o["height"].toInt();
    p.tracks.clear();
    if (!o["media"].isArray() || !o["tracks"].isArray() || !o["clips"].isArray())
        throw Error("Missing project arrays");
    for (auto v : o["media"].toArray()) {
        auto m = v.toObject();
        p.media.append({m["id"].toString(),
                        QDir(QFileInfo(path).absolutePath()).absoluteFilePath(m["path"].toString()),
                        integer(m["durationUs"]), m["width"].toInt(), m["height"].toInt(),
                        m["video"].toBool(), m["audio"].toBool()});
    }
    for (auto v : o["tracks"].toArray()) {
        auto t = v.toObject();
        p.tracks.append({t["name"].toString(), t["kind"].toString(), t["muted"].toBool(), t["solo"].toBool(),
                         t["locked"].toBool()});
    }
    for (auto v : o["clips"].toArray()) {
        auto c = v.toObject();
        p.clips.append({c["id"].toString(), c["media"].toString(), c["track"].toInt(-1), integer(c["start"]),
                        integer(c["in"]), integer(c["duration"])});
    }
    p.validate();
    return p;
}
} // namespace ff
