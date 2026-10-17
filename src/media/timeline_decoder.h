// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/project.h"
#include <QByteArray>
#include <QImage>
#include <functional>
#include <memory>
namespace ff {
// Worker-thread only. No GUI objects, processes, encoders or output files.
class TimelineDecoder {
  public:
    TimelineDecoder(Project project, QSize size, std::function<bool()> cancelled = {});
    ~TimelineDecoder();
    QImage video(qint64 frame);
    // Stereo interleaved float PCM, 48 kHz. Sample offsets are on the timeline.
    QByteArray audio(qint64 startSample, int samples);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ff
