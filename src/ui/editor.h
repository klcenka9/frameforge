// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/project.h"
#include "media/render.h"
#include "media/timeline_player.h"
#include <QAudioOutput>
#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QTimer>
#include <QUndoStack>
#include <functional>
class QListWidget;
class QLabel;
class QSpinBox;
class QComboBox;
class QProgressBar;
class QDockWidget;
class QSlider;
class QScrollArea;
class QVideoWidget;
class QAction;
class QStackedWidget;
namespace ff {
class Timeline;
class MediaPool;
class VideoMonitor;
class Editor : public QMainWindow {
    Q_OBJECT
  public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;
    void apply(const Project &p);
    friend class EditorTests;

  protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    Project project;
    QUndoStack history;
    QString projectPath, selection;
    quint64 epoch = 0;
    bool timelinePlayback = false;
    TimelinePlayer timelinePlayer;
    QHash<QString, QImage> thumbnails;
    QMediaPlayer player;
    QAudioOutput audio;
    RenderJob render;
    QTimer autosave;
    MediaPool *pool;
    QScrollArea *timelineScroll;
    QStackedWidget *viewer;
    VideoMonitor *timelineMonitor;
    QLabel *metadata, *position, *previewLabel, *exportInfo;
    Timeline *timeline;
    QSpinBox *clipIn, *clipDuration, *clipStart;
    QComboBox *clipTrack, *codec;
    QProgressBar *progress;
    QSlider *scrub;
    QDockWidget *mediaDock, *timelineDock, *inspectorDock, *deliverDock;
    QString recoveryPath() const;
    void mutate(const QString &label, const std::function<void(Project &)> &fn);
    void refresh();
    void inspect();
    void importFiles();
    void importDroppedFiles(const QStringList &, int track = -1, qint64 frame = 0);
    void playTimeline();
    void playSource();
    void importPath(const QString &path, const QString &relinkId = {});
    void sourceSelected();
    void addClip();
    void split();
    void removeClip();
    void save(bool as = false);
    void open();
    void newProject();
    bool canDiscard();
    void renderTimeline();
    void settings();
    void error(const QString &message);
    void recover();
    void writeRecovery();
    void seekFrame(qint64 frame);
};
} // namespace ff
