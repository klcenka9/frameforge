// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QStringList>
#include <QUrl>
namespace ff {
inline constexpr auto mediaIdMime = "application/x-frameforge-media-id";
inline void acceptMediaCopy(QDropEvent *event) {
    if (event->possibleActions().testFlag(Qt::CopyAction)) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else
        event->ignore();
}
inline QStringList droppedFiles(const QMimeData *mime) {
    QStringList paths;
    for (const auto &url : mime->urls())
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
            paths.append(QFileInfo(url.toLocalFile()).absoluteFilePath());
    return paths;
}
inline bool hasMediaId(const QMimeData *mime) {
    return !mime->data(mediaIdMime).isEmpty() && mime->data(mediaIdMime).size() < 256;
}
} // namespace ff
