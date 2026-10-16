// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/project.h"
#include "media/render.h"
#include <QAudioOutput>
#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QTemporaryDir>
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
class QVideoWidget;
class QAction;
namespace ff {
class Timeline;
class Editor : public QMainWindow {
    Q_OBJECT
  public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;
    void apply(const Project &p);
    friend class EditorTests;

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    Project project;
    QUndoStack history;
    QString projectPath, selection;
    quint64 epoch = 0;
    bool timelinePlayback = false, previewValid = false, previewRender = false;
    QTemporaryDir previewDirectory;
    QString previewPath;
    QHash<QString, QImage> thumbnails;
    QMediaPlayer player;
    QAudioOutput audio;
    RenderJob render;
    QTimer autosave;
    QListWidget *pool;
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
    void importPath(const QString &path, const QString &relinkId = {});
    void sourceSelected();
    void addClip();
    void split();
    void removeClip();
    void save(bool as = false);
    void open();
    void newProject();
    bool canDiscard();
    void renderTimeline(bool exporting);
    void settings();
    void error(const QString &message);
    void recover();
    void writeRecovery();
    void seekFrame(qint64 frame);
};
} // namespace ff
