// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/project.h"
#include <QWidget>
namespace ff {
class Timeline : public QWidget {
    Q_OBJECT
  public:
    explicit Timeline(QWidget *parent = nullptr);
    void setProject(const Project &p);
    void setSelected(const QString &id);
    void setPlayhead(qint64 frame);
    void setZoom(int value);
    qint64 playhead() const {
        return head;
    }
    bool snapping = true;
    int playheadX() const;
  signals:
    void selected(const QString &id);
    void seek(qint64 frame);
    void moved(const QString &id, int track, qint64 frame);
    void trackToggled(int track, int control);
    void mediaDropped(const QString &id, int track, qint64 frame);
    void filesDropped(const QStringList &paths, int track, qint64 frame);

  protected:
    void paintEvent(QPaintEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

  private:
    Project project;
    QString selection, dragId;
    qint64 head = 0, dragOffset = 0;
    double pixels = 4;
    QPoint press;
    qint64 frameAt(int x) const;
    QRectF rectangle(const Clip &c) const;
    void geometryChanged();
};
} // namespace ff
