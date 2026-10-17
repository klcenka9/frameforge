// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/project.h"
#include <QAudioDevice>
#include <QImage>
#include <QObject>
#include <memory>
namespace ff {
class TimelinePlayer : public QObject {
    Q_OBJECT
  public:
    explicit TimelinePlayer(QObject *parent = nullptr);
    ~TimelinePlayer() override;
    void setProject(const Project &project);
    void setAudioDevice(const QAudioDevice &device);
    void setPreviewSize(QSize size);
    void play();
    void pause();
    void seek(qint64 frame);
    qint64 frame() const;
    bool isPlaying() const;
  signals:
    void frameReady(const QImage &image);
    void positionChanged(qint64 frame);
    void playingChanged(bool playing);
    void bufferingChanged(bool buffering);
    void ended();
    void error(const QString &message);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ff
