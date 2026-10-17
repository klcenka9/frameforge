// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QPainter>
#include <QWidget>
namespace ff {
class VideoMonitor : public QWidget {
    QImage image;

  public:
    explicit VideoMonitor(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(320, 180);
        setObjectName("timelineMonitor");
        setAccessibleName("Live timeline monitor");
    }
    void setFrame(const QImage &frame) {
        image = frame;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (!image.isNull()) {
            const auto fit = image.size().scaled(size(), Qt::KeepAspectRatio);
            painter.drawImage(QRect(QPoint((width() - fit.width()) / 2, (height() - fit.height()) / 2), fit),
                              image);
        }
    }
};
} // namespace ff
