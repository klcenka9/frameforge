// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <stdexcept>
namespace ff {
struct Error : std::runtime_error {
    explicit Error(const QString &s) : std::runtime_error(s.toStdString()) {}
};
struct Rate {
    int num = 25, den = 1;
    qint64 us(qint64 frames) const;
    qint64 frames(qint64 us) const;
    QString text() const;
};
struct Media {
    QString id, path;
    qint64 durationUs = 0;
    int width = 0, height = 0;
    bool video = false, audio = false;
};
struct Track {
    QString name, kind;
    bool muted = false, solo = false, locked = false;
};
struct Clip {
    QString id, media;
    int track = 0;
    qint64 start = 0, in = 0, duration = 0;
};
struct Project {
    Rate rate;
    int width = 1280, height = 720;
    QVector<Media> media;
    QVector<Track> tracks{{"V1", "video"}, {"V2", "video"}, {"A1", "audio"}, {"A2", "audio"}};
    QVector<Clip> clips;
    const Media &source(const QString &id) const;
    const Clip &clip(const QString &id) const;
    Clip &clip(const QString &id);
    qint64 end() const;
    bool audibleTrack(int track) const;
    void validate() const;
    QString split(const QString &id, qint64 frame);
    void move(const QString &id, int track, qint64 frame);
    void trim(const QString &id, qint64 in, qint64 duration);
    void erase(const QString &id);
    QJsonObject json(const QString &directory) const;
    void save(const QString &path) const;
    static Project load(const QString &path);
};
QString uuid();
} // namespace ff
