// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QListWidget>
namespace ff {
class MediaPool : public QListWidget {
    Q_OBJECT
  public:
    explicit MediaPool(QWidget *parent = nullptr);
    QStringList mimeTypes() const override;
    Qt::DropActions supportedDropActions() const override {
        return Qt::CopyAction;
    }
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
  signals:
    void filesDropped(const QStringList &paths);

  protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
};
} // namespace ff
