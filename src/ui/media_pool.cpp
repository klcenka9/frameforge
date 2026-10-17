// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_pool.h"
#include "media_drop.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
namespace ff {
MediaPool::MediaPool(QWidget *parent) : QListWidget(parent) {
    setToolTip("Drop local media files here to import. Drag a media item onto a timeline track to edit it.");
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
}
QStringList MediaPool::mimeTypes() const {
    return {mediaIdMime, "text/uri-list"};
}
QMimeData *MediaPool::mimeData(const QList<QListWidgetItem *> &items) const {
    auto *mime = new QMimeData;
    if (!items.isEmpty()) {
        mime->setData(mediaIdMime, items.first()->data(Qt::UserRole).toString().toUtf8());
        mime->setUrls({QUrl::fromLocalFile(items.first()->data(Qt::UserRole + 1).toString())});
    }
    return mime;
}
void MediaPool::dragEnterEvent(QDragEnterEvent *event) {
    if (!hasMediaId(event->mimeData()) && !droppedFiles(event->mimeData()).isEmpty())
        acceptMediaCopy(event);
}
void MediaPool::dragMoveEvent(QDragMoveEvent *event) {
    if (!hasMediaId(event->mimeData()) && !droppedFiles(event->mimeData()).isEmpty())
        acceptMediaCopy(event);
}
void MediaPool::dropEvent(QDropEvent *event) {
    if (!event->possibleActions().testFlag(Qt::CopyAction)) {
        event->ignore();
        return;
    }
    auto paths = droppedFiles(event->mimeData());
    if (!hasMediaId(event->mimeData()) && !paths.isEmpty()) {
        emit filesDropped(paths);
        acceptMediaCopy(event);
    }
}
} // namespace ff
