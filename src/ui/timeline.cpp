// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline.h"
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
namespace ff {
Timeline::Timeline(QWidget *p) : QWidget(p) {
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Multitrack timeline");
    setToolTip(
        "Click ruler to seek; drag clips to move. M: mute, S: solo, L: lock. Exact trims are in Inspector.");
    geometryChanged();
}
void Timeline::setProject(const Project &p) {
    project = p;
    geometryChanged();
    update();
}
void Timeline::setSelected(const QString &id) {
    selection = id;
    update();
}
void Timeline::setPlayhead(qint64 f) {
    head = std::max(qint64(0), f);
    update();
}
void Timeline::setZoom(int z) {
    pixels = z / 10.0;
    geometryChanged();
    update();
}
void Timeline::geometryChanged() {
    setMinimumSize(int(std::min(100000.0, std::max(1000.0, 160 + (project.end() + 100) * pixels))),
                   48 + project.tracks.size() * 68);
}
qint64 Timeline::frameAt(int x) const {
    return std::max(qint64(0), qint64(std::llround((x - 160) / pixels)));
}
QRectF Timeline::rectangle(const Clip &c) const {
    return {160 + c.start * pixels, 48.0 + c.track * 68, std::max(2.0, c.duration * pixels), 58};
}
void Timeline::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#17191d"));
    p.setFont(QFont("Sans Serif", 9));
    int step = std::max(1, int(100 / pixels));
    for (qint64 f = 0; 160 + f * pixels < width(); f += step) {
        int x = int(160 + f * pixels);
        p.setPen(QColor("#353940"));
        p.drawLine(x, 28, x, height());
        p.setPen(QColor("#aeb4bf"));
        p.drawText(x + 4, 19, QString::number(f));
    }
    for (int i = 0; i < project.tracks.size(); ++i) {
        int y = 48 + i * 68;
        p.fillRect(0, y, 158, 64, QColor("#252830"));
        const auto &t = project.tracks[i];
        p.setPen(Qt::white);
        p.drawText(10, y + 20, t.name + " · " + t.kind);
        for (int k = 0; k < 3; ++k) {
            bool on = k == 0 ? t.muted : k == 1 ? t.solo : t.locked;
            p.setPen(on ? QColor("#ffc66d") : QColor("#a6acb7"));
            p.drawText(16 + k * 42, y + 47, QString(k == 0 ? "M" : k == 1 ? "S" : "L"));
        }
    }
    for (const auto &c : project.clips) {
        auto r = rectangle(c);
        p.setBrush(project.tracks[c.track].kind == "video" ? QColor("#315d7d") : QColor("#38654e"));
        p.setPen(QPen(c.id == selection ? QColor("#f6bc66") : QColor("#5c7a90"), c.id == selection ? 2 : 1));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 3, 3);
        p.setPen(Qt::white);
        auto name = QFileInfo(project.source(c.media).path).fileName();
        p.save();
        p.setClipRect(r.adjusted(5, 3, -4, -3));
        p.drawText(r.adjusted(7, 4, -4, -4), Qt::AlignTop | Qt::AlignLeft, name);
        p.drawText(r.adjusted(7, 25, -4, -4), Qt::AlignTop | Qt::AlignLeft,
                   QString("IN %1 · %2 frames").arg(c.in).arg(c.duration));
        p.restore();
    }
    p.setPen(QPen(QColor("#ee6b63"), 2));
    int x = int(160 + head * pixels);
    p.drawLine(x, 25, x, height());
    p.setBrush(QColor("#ee6b63"));
    p.drawPolygon(QPolygon{{x - 5, 25}, {x + 5, 25}, {x, 32}});
}
void Timeline::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton)
        return;
    setFocus();
    press = e->pos();
    if (e->position().x() < 160 && e->position().y() >= 48) {
        int t = (e->position().y() - 48) / 68;
        int k = (e->position().x() - 6) / 42;
        if (t >= 0 && t < project.tracks.size() && e->position().y() - (48 + t * 68) > 28 && k >= 0 && k < 3)
            emit trackToggled(t, k);
        return;
    }
    for (const auto &c : project.clips)
        if (rectangle(c).contains(e->position())) {
            selection = c.id;
            dragId = c.id;
            dragOffset = frameAt(e->position().x()) - c.start;
            emit selected(c.id);
            update();
            return;
        }
    head = frameAt(e->position().x());
    emit seek(head);
    update();
}
void Timeline::mouseMoveEvent(QMouseEvent *e) {
    if (dragId.isEmpty() && (e->buttons() & Qt::LeftButton) && e->position().x() >= 160) {
        head = frameAt(e->position().x());
        emit seek(head);
        update();
    }
}
void Timeline::mouseReleaseEvent(QMouseEvent *e) {
    if (dragId.isEmpty())
        return;
    auto id = dragId;
    dragId.clear();
    if ((e->pos() - press).manhattanLength() < 4)
        return;
    auto start = std::max(qint64(0), frameAt(e->position().x()) - dragOffset);
    int track = std::clamp(int((e->position().y() - 48) / 68), 0, int(project.tracks.size() - 1));
    if (snapping) {
        QVector<qint64> edges{0, head};
        for (const auto &c : project.clips)
            if (c.id != id) {
                edges << c.start << c.start + c.duration;
            }
        for (auto edge : edges) {
            if (std::abs(double(start - edge) * pixels) < 8)
                start = edge;
            auto end = start + project.clip(id).duration;
            if (std::abs(double(end - edge) * pixels) < 8)
                start = std::max(qint64(0), edge - project.clip(id).duration);
        }
    }
    emit moved(id, track, start);
}
} // namespace ff
