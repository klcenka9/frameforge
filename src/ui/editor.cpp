// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor.h"
#include "media/media.h"
#include "media_drop.h"
#include "media_pool.h"
#include "timeline.h"
#include <QFutureWatcher>
#include <QSettings>
#include <QStandardPaths>
#include <QVideoWidget>
#include <QtConcurrent>
#include <QtWidgets>
#include <functional>
namespace ff {
class Snapshot final : public QUndoCommand {
    Editor *editor;
    Project before, after;

  public:
    Snapshot(Editor *e, Project b, Project a, const QString &label)
        : QUndoCommand(label), editor(e), before(std::move(b)), after(std::move(a)) {}
    void undo() override {
        editor->apply(before);
    }
    void redo() override {
        editor->apply(after);
    }
};
static QDockWidget *dock(QMainWindow *w, const QString &title, const QString &name, QWidget *body,
                         Qt::DockWidgetArea area) {
    auto *d = new QDockWidget(title, w);
    d->setObjectName(name);
    d->setWidget(body);
    w->addDockWidget(area, d);
    return d;
}
Editor::Editor(QWidget *parent) : QMainWindow(parent), render(this) {
    setObjectName("FrameforgeEditor");
    setAcceptDrops(true);
    resize(1440, 900);
    setMinimumSize(900, 600);
    setDockNestingEnabled(true);
    auto *file = menuBar()->addMenu("&File");
    auto action = [this](QMenu *m, const QString &name, const QKeySequence &key, auto fn) {
        auto *a = m->addAction(name);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    action(file, "&New project", QKeySequence::New, [this] { newProject(); });
    action(file, "&Open project…", QKeySequence::Open, [this] { open(); });
    action(file, "&Save", QKeySequence::Save, [this] { save(); });
    action(file, "Save as…", QKeySequence::SaveAs, [this] { save(true); });
    file->addSeparator();
    action(file, "Import media…", QKeySequence("Ctrl+I"), [this] { importFiles(); });
    action(file, "Project settings…", {}, [this] { settings(); });
    action(file, "Recover autosave…", {}, [this] { recover(); });
    action(file, "Quit", QKeySequence::Quit, [this] { close(); });
    auto *edit = menuBar()->addMenu("&Edit");
    auto *undo = history.createUndoAction(this, "Undo");
    undo->setShortcut(QKeySequence::Undo);
    edit->addAction(undo);
    auto *redo = history.createRedoAction(this, "Redo");
    redo->setShortcut(QKeySequence::Redo);
    edit->addAction(redo);
    action(edit, "Split at playhead", QKeySequence("Ctrl+B"), [this] { split(); });
    action(edit, "Delete selected clip", QKeySequence::Delete, [this] { removeClip(); });
    action(edit, "Add selected media to timeline", QKeySequence("Ctrl+Return"), [this] { addClip(); });
    auto *toolbar = addToolBar("Editing");
    toolbar->setObjectName("EditingToolbar");
    toolbar->addAction("Import", this, [this] { importFiles(); });
    toolbar->addAction("Add clip", this, [this] { addClip(); });
    toolbar->addAction("Split", this, [this] { split(); });
    toolbar->addAction("Delete", this, [this] { removeClip(); });
    auto *snap = toolbar->addAction("Snap");
    snap->setCheckable(true);
    snap->setChecked(true);
    auto *center = new QWidget;
    auto *layout = new QVBoxLayout(center);
    layout->setContentsMargins(6, 6, 6, 6);
    previewLabel = new QLabel("SOURCE / TIMELINE VIEWER · CPU rendering");
    layout->addWidget(previewLabel);
    auto *video = new QVideoWidget;
    video->setMinimumSize(320, 180);
    video->setAccessibleName("Video preview monitor");
    layout->addWidget(video, 1);
    player.setVideoOutput(video);
    player.setAudioOutput(&audio);
    audio.setVolume(.7);
    auto *transport = new QHBoxLayout;
    auto *back = new QPushButton("◀ Frame");
    auto *play = new QPushButton("Play timeline / Pause");
    play->setObjectName("playTimeline");
    play->setToolTip("Play edited timeline from the playhead; builds a preview in the background if needed");
    auto *sourcePlay = new QPushButton("Play source / Pause");
    sourcePlay->setObjectName("playSource");
    auto *next = new QPushButton("Frame ▶");
    auto *preview = new QPushButton("Render timeline preview");
    preview->setToolTip("Render edited timeline in background, then play. Editing invalidates it.");
    transport->addWidget(back);
    transport->addWidget(play);
    transport->addWidget(sourcePlay);
    transport->addWidget(next);
    transport->addWidget(preview);
    position = new QLabel("0 / 0");
    transport->addWidget(position);
    layout->addLayout(transport);
    scrub = new QSlider(Qt::Horizontal);
    scrub->setAccessibleName("Viewer position");
    layout->addWidget(scrub);
    setCentralWidget(center);
    connect(play, &QPushButton::clicked, this, &Editor::playTimeline);
    connect(sourcePlay, &QPushButton::clicked, this, &Editor::playSource);
    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, play, &QPushButton::click);
    connect(back, &QPushButton::clicked, this, [this] {
        player.pause();
        player.setPosition(std::max(qint64(0), player.position() - project.rate.us(1) / 1000));
    });
    connect(next, &QPushButton::clicked, this, [this] {
        player.pause();
        player.setPosition(player.position() + project.rate.us(1) / 1000);
    });
    connect(preview, &QPushButton::clicked, this, [this] { renderTimeline(false); });
    connect(scrub, &QSlider::sliderMoved, &player, &QMediaPlayer::setPosition);
    connect(&player, &QMediaPlayer::durationChanged, this,
            [this](qint64 d) { scrub->setRange(0, int(std::min(d, qint64(INT_MAX)))); });
    connect(&player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        if (!scrub->isSliderDown())
            scrub->setValue(int(ms));
        position->setText(
            QString("%1 / %2 s").arg(ms / 1000.0, 0, 'f', 2).arg(player.duration() / 1000.0, 0, 'f', 2));
        if (timelinePlayback && pendingPreviewFrame < 0) {
            timeline->setPlayhead(project.rate.frames(ms * 1000));
            auto *bar = timelineScroll->horizontalScrollBar();
            const int x = timeline->playheadX();
            const int visible = timelineScroll->viewport()->width();
            if (x < bar->value() || x > bar->value() + visible - 20)
                bar->setValue(std::max(0, x - visible / 3));
        }
    });
    connect(&player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &s) {
        statusBar()->showMessage("Playback error: " + s, 15000);
    });
    connect(&player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (timelinePlayback && pendingPreviewFrame >= 0 &&
            (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
            const auto frame = pendingPreviewFrame;
            pendingPreviewFrame = -1;
            player.setPosition(project.rate.us(frame) / 1000);
            player.play();
        }
    });
    auto *mediaBody = new QWidget;
    auto *ml = new QVBoxLayout(mediaBody);
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search imported media");
    search->setAccessibleName("Search media");
    pool = new MediaPool;
    connect(pool, &MediaPool::filesDropped, this,
            [this](const QStringList &files) { importDroppedFiles(files); });
    pool->setIconSize({96, 54});
    pool->setMinimumWidth(230);
    pool->setAccessibleName("Project media pool");
    metadata = new QLabel("Import video or audio to begin.");
    metadata->setWordWrap(true);
    auto *relink = new QPushButton("Relink selected media…");
    ml->addWidget(search);
    ml->addWidget(pool, 1);
    ml->addWidget(metadata);
    ml->addWidget(relink);
    mediaDock = dock(this, "Media Pool", "MediaPool", mediaBody, Qt::LeftDockWidgetArea);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &s) {
        for (int i = 0; i < pool->count(); ++i)
            pool->item(i)->setHidden(!pool->item(i)->text().contains(s, Qt::CaseInsensitive));
    });
    connect(pool, &QListWidget::currentRowChanged, this, [this] { sourceSelected(); });
    connect(pool, &QListWidget::itemDoubleClicked, this, [this] { addClip(); });
    connect(relink, &QPushButton::clicked, this, [this] {
        auto *item = pool->currentItem();
        if (!item)
            return;
        auto path = QFileDialog::getOpenFileName(this, "Relink media");
        if (!path.isEmpty())
            importPath(path, item->data(Qt::UserRole).toString());
    });
    timeline = new Timeline;
    auto *scroll = new QScrollArea;
    timelineScroll = scroll;
    scroll->setWidget(timeline);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(245);
    timelineDock =
        dock(this, "Timeline · integer project frames", "Timeline", scroll, Qt::BottomDockWidgetArea);
    connect(snap, &QAction::toggled, timeline, [this](bool on) { timeline->snapping = on; });
    auto *zoom = new QSlider(Qt::Horizontal);
    zoom->setRange(2, 100);
    zoom->setValue(40);
    zoom->setMaximumWidth(150);
    zoom->setToolTip("Timeline zoom");
    toolbar->addWidget(zoom);
    connect(zoom, &QSlider::valueChanged, timeline, &Timeline::setZoom);
    connect(timeline, &Timeline::selected, this, [this](const QString &id) {
        selection = id;
        inspect();
    });
    connect(timeline, &Timeline::seek, this, &Editor::seekFrame);
    connect(timeline, &Timeline::filesDropped, this, &Editor::importDroppedFiles);
    connect(timeline, &Timeline::mediaDropped, this, [this](const QString &id, int track, qint64 frame) {
        mutate("Drop media clip", [&](Project &p) {
            if (p.tracks[track].locked)
                throw Error("Track is locked");
            const auto &m = p.source(id);
            selection = uuid();
            p.clips.append({selection, id, track, frame, 0, p.rate.frames(m.durationUs)});
        });
    });
    connect(timeline, &Timeline::moved, this, [this](const QString &id, int track, qint64 frame) {
        mutate("Move clip", [&](Project &p) { p.move(id, track, frame); });
    });
    connect(timeline, &Timeline::trackToggled, this, [this](int t, int k) {
        mutate("Track state", [&](Project &p) {
            if (k == 0)
                p.tracks[t].muted = !p.tracks[t].muted;
            if (k == 1)
                p.tracks[t].solo = !p.tracks[t].solo;
            if (k == 2)
                p.tracks[t].locked = !p.tracks[t].locked;
        });
    });
    auto *inspector = new QWidget;
    auto *form = new QFormLayout(inspector);
    clipTrack = new QComboBox;
    clipIn = new QSpinBox;
    clipDuration = new QSpinBox;
    clipStart = new QSpinBox;
    for (auto *s : {clipIn, clipDuration, clipStart})
        s->setRange(0, 1000000000);
    clipDuration->setMinimum(1);
    form->addRow("Track", clipTrack);
    form->addRow("Source in (frames)", clipIn);
    form->addRow("Duration (frames)", clipDuration);
    form->addRow("Timeline start", clipStart);
    auto *apply = new QPushButton("Apply clip edit");
    form->addRow(apply);
    auto *help = new QLabel("Select a clip, then edit exact frame values. Drag to move. Tracks: M mute, S "
                            "solo, L lock. Video clips include source audio.");
    help->setWordWrap(true);
    form->addRow(help);
    inspectorDock = dock(this, "Inspector", "Inspector", inspector, Qt::RightDockWidgetArea);
    connect(apply, &QPushButton::clicked, this, [this] {
        mutate("Edit clip", [&](Project &p) {
            auto old = p.clip(selection);
            if (p.tracks[old.track].locked)
                throw Error("Track is locked");
            auto &c = p.clip(selection);
            c.in = clipIn->value();
            c.duration = clipDuration->value();
            p.move(selection, clipTrack->currentIndex(), clipStart->value());
        });
    });
    auto *delivery = new QWidget;
    auto *dl = new QVBoxLayout(delivery);
    exportInfo = new QLabel;
    exportInfo->setWordWrap(true);
    codec = new QComboBox;
    codec->setAccessibleName("Software video encoder");
    auto *exportButton = new QPushButton("Export new MP4…");
    progress = new QProgressBar;
    auto *cancel = new QPushButton("Cancel render");
    dl->addWidget(exportInfo);
    dl->addWidget(new QLabel("CPU video encoder · AAC 192 kb/s"));
    dl->addWidget(codec);
    dl->addWidget(exportButton);
    dl->addWidget(progress);
    dl->addWidget(cancel);
    dl->addStretch();
    deliverDock = dock(this, "Deliver", "Deliver", delivery, Qt::RightDockWidgetArea);
    tabifyDockWidget(inspectorDock, deliverDock);
    inspectorDock->raise();
    connect(exportButton, &QPushButton::clicked, this, [this] { renderTimeline(true); });
    connect(cancel, &QPushButton::clicked, &render, &RenderJob::cancel);
    connect(&render, &RenderJob::progress, progress, &QProgressBar::setValue);
    connect(&render, &RenderJob::finished, this, [this](bool ok, const QString &message) {
        if (ok && previewRender) {
            previewPath = message;
            previewValid = true;
            if (pendingPreviewFrame >= 0)
                loadTimelinePreview();
            else
                statusBar()->showMessage("Timeline preview ready", 4000);
        } else if (ok) {
            statusBar()->showMessage("Export validated: " + message, 15000);
            QMessageBox::information(this, "Export complete", "Validated video saved to:\n" + message);
        } else {
            pendingPreviewFrame = -1;
            error(message);
        }
    });
    auto *workspace = menuBar()->addMenu("&Workspace");
    for (auto *d : {mediaDock, timelineDock, inspectorDock, deliverDock})
        workspace->addAction(d->toggleViewAction());
    workspace->addSeparator();
    action(workspace, "Save dock layout", {}, [this] { QSettings().setValue("layout", saveState()); });
    action(workspace, "Restore dock layout", {},
           [this] { restoreState(QSettings().value("layout").toByteArray()); });
    auto *pages = addToolBar("Workspaces");
    pages->setObjectName("WorkspaceBar");
    addToolBar(Qt::BottomToolBarArea, pages);
    for (const auto &name : {"Media", "Cut", "Edit", "Deliver"}) {
        auto *a = pages->addAction(name);
        connect(a, &QAction::triggered, this, [this, name] {
            mediaDock->setVisible(true);
            timelineDock->setVisible(QString(name) != "Media");
            if (QString(name) == "Deliver") {
                deliverDock->show();
                deliverDock->raise();
            } else {
                inspectorDock->show();
                inspectorDock->raise();
            }
        });
    }
    for (const auto &name : {"Fusion", "Color", "Fairlight"}) {
        auto *a = pages->addAction(QString(name) + " · planned");
        a->setEnabled(false);
        a->setToolTip("Not implemented in this milestone");
    }
    auto *helpMenu = menuBar()->addMenu("&Help");
    action(helpMenu, "Backend diagnostics…", {}, [this] {
        auto *w = new QFutureWatcher<QString>(this);
        connect(w, &QFutureWatcher<QString>::finished, this, [this, w] {
            QMessageBox::information(this, "Backend diagnostics", w->result());
            w->deleteLater();
        });
        w->setFuture(QtConcurrent::run([] { return diagnostics(); }));
    });
    action(helpMenu, "About", {}, [this] {
        QMessageBox::about(
            this, "Frameforge",
            "Frameforge 0.1 — experimental native Linux editor\nIndependent open-source implementation.\nCPU "
            "render, source playback and rendered timeline preview.\nFusion, grading, automation and "
            "professional audio are planned.\nNo Mint/GPU compatibility certification yet.");
    });
    connect(&history, &QUndoStack::cleanChanged, this,
            [this](bool) { setWindowModified(!history.isClean()); });
    autosave.setInterval(30000);
    connect(&autosave, &QTimer::timeout, this, &Editor::writeRecovery);
    autosave.start();
    auto *w = new QFutureWatcher<QStringList>(this);
    connect(w, &QFutureWatcher<QStringList>::finished, this, [this, w] {
        auto list = w->result();
        if (list.contains("aac")) {
            for (auto c : {"libx264", "mpeg4"})
                if (list.contains(c))
                    codec->addItem(c);
        }
        if (codec->count() == 0)
            statusBar()->showMessage("No supported MP4/AAC encoders found. Check FFmpeg installation.",
                                     15000);
        w->deleteLater();
    });
    w->setFuture(QtConcurrent::run([] {
        try {
            return encoders();
        } catch (...) {
            return QStringList{};
        }
    }));
    refresh();
    if (QSettings().contains("layout"))
        restoreState(QSettings().value("layout").toByteArray());
    QTimer::singleShot(0, this, [this] {
        if (QFileInfo::exists(recoveryPath()) &&
            QMessageBox::question(this, "Recovery available", "An autosave exists. Recover it?") ==
                QMessageBox::Yes)
            recover();
    });
}
Editor::~Editor() {
    // Detach while both the sink widget and audio output are still alive.
    disconnect(&player, nullptr, this, nullptr);
    player.stop();
    player.setVideoOutput(nullptr);
    player.setAudioOutput(nullptr);
    player.setSource({});
}
void Editor::error(const QString &s) {
    QMessageBox::warning(this, "Frameforge", s);
}
void Editor::mutate(const QString &label, const std::function<void(Project &)> &fn) {
    try {
        if (render.busy() && previewRender)
            throw Error("Cancel the preview render before editing");
        auto next = project;
        fn(next);
        next.validate();
        history.push(new Snapshot(this, project, next, label));
    } catch (const std::exception &e) {
        error(QString::fromUtf8(e.what()));
    }
}
void Editor::apply(const Project &p) {
    project = p;
    previewValid = false;
    pendingPreviewFrame = -1;
    if (timelinePlayback) {
        timelinePlayback = false;
        player.stop();
        player.setSource({});
        previewLabel->setText("Timeline changed · press Play timeline to rebuild preview");
    }
    refresh();
}
void Editor::refresh() {
    QString current = pool->currentItem() ? pool->currentItem()->data(Qt::UserRole).toString() : QString{};
    QSignalBlocker block(pool);
    pool->clear();
    for (const auto &m : project.media) {
        auto *i = new QListWidgetItem(QFileInfo(m.path).fileName(), pool);
        i->setData(Qt::UserRole, m.id);
        i->setData(Qt::UserRole + 1, m.path);
        i->setToolTip(m.path);
        if (!QFileInfo::exists(m.path)) {
            i->setForeground(QColor("#f48b82"));
            i->setText("Missing · " + i->text());
        }
        if (thumbnails.contains(m.id))
            i->setIcon(QPixmap::fromImage(thumbnails.value(m.id)));
        if (m.id == current)
            pool->setCurrentItem(i);
    }
    timeline->setProject(project);
    timeline->setSelected(selection);
    clipTrack->clear();
    for (auto &t : project.tracks)
        clipTrack->addItem(t.name);
    inspect();
    exportInfo->setText(QString("%1 × %2 · %3 fps\nTimeline: %4 frames\nMP4 · software rendering\nExisting "
                                "files are never overwritten.")
                            .arg(project.width)
                            .arg(project.height)
                            .arg(project.rate.text())
                            .arg(project.end()));
    setWindowTitle(QString("%1[*] — Frameforge")
                       .arg(projectPath.isEmpty() ? "Untitled" : QFileInfo(projectPath).fileName()));
}
void Editor::inspect() {
    try {
        auto c = project.clip(selection);
        clipTrack->setCurrentIndex(c.track);
        clipIn->setValue(int(c.in));
        clipDuration->setValue(int(c.duration));
        clipStart->setValue(int(c.start));
    } catch (const Error &) {
    }
}
struct Analysis {
    Media media;
    QImage image;
    QString error;
};
void Editor::importFiles() {
    for (const auto &path : QFileDialog::getOpenFileNames(this, "Import media"))
        importPath(path);
}
void Editor::importPath(const QString &path, const QString &relinkId) {
    auto generation = epoch;
    auto *w = new QFutureWatcher<Analysis>(this);
    statusBar()->showMessage("Analyzing " + QFileInfo(path).fileName());
    connect(w, &QFutureWatcher<Analysis>::finished, this, [this, w, generation, relinkId] {
        auto result = w->result();
        w->deleteLater();
        if (generation != epoch)
            return;
        if (!result.error.isEmpty()) {
            error(result.error);
            return;
        }
        auto id = result.media.id;
        if (thumbnails.size() >= 256)
            thumbnails.clear();
        if (!result.image.isNull())
            thumbnails.insert(relinkId.isEmpty() ? id : relinkId, result.image);
        mutate(relinkId.isEmpty() ? "Import media" : "Relink media", [&](Project &p) {
            if (relinkId.isEmpty())
                p.media.append(result.media);
            else {
                bool found = false;
                for (auto &m : p.media)
                    if (m.id == relinkId) {
                        auto replacement = result.media;
                        replacement.id = m.id;
                        m = replacement;
                        id = m.id;
                        found = true;
                        break;
                    }
                if (!found)
                    throw Error("Media was removed before relinking finished");
            }
        });
        for (int i = 0; i < pool->count(); ++i)
            if (pool->item(i)->data(Qt::UserRole).toString() == id) {
                if (!result.image.isNull())
                    pool->item(i)->setIcon(QPixmap::fromImage(result.image));
                {
                    QSignalBlocker block(pool);
                    pool->setCurrentRow(i);
                }
                sourceSelected();
            }
        statusBar()->showMessage("Media analysis finished", 3000);
    });
    w->setFuture(QtConcurrent::run([path] {
        Analysis r;
        try {
            r.media = probe(path);
            if (r.media.video)
                r.image = thumbnail(path);
        } catch (const std::exception &e) {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}
void Editor::dragEnterEvent(QDragEnterEvent *event) {
    if (!hasMediaId(event->mimeData()) && !droppedFiles(event->mimeData()).isEmpty())
        acceptMediaCopy(event);
}
void Editor::dragMoveEvent(QDragMoveEvent *event) {
    if (!hasMediaId(event->mimeData()) && !droppedFiles(event->mimeData()).isEmpty())
        acceptMediaCopy(event);
}
void Editor::dropEvent(QDropEvent *event) {
    if (!event->possibleActions().testFlag(Qt::CopyAction)) {
        event->ignore();
        return;
    }
    auto paths = droppedFiles(event->mimeData());
    if (!hasMediaId(event->mimeData()) && !paths.isEmpty()) {
        importDroppedFiles(paths);
        acceptMediaCopy(event);
    }
}
void Editor::importDroppedFiles(const QStringList &paths, int track, qint64 frame) {
    if (paths.isEmpty())
        return;
    if (render.busy() && previewRender) {
        statusBar()->showMessage("Cancel timeline preview rendering before dropping files", 5000);
        return;
    }
    const auto generation = epoch;
    auto *watcher = new QFutureWatcher<QVector<Analysis>>(this);
    statusBar()->showMessage(QString("Importing %1 dropped file(s)…").arg(paths.size()));
    connect(watcher, &QFutureWatcher<QVector<Analysis>>::finished, this,
            [this, watcher, generation, track, frame] {
                const auto results = watcher->result();
                watcher->deleteLater();
                if (generation != epoch)
                    return;
                QStringList errors;
                QVector<Media> imported;
                for (const auto &result : results) {
                    if (!result.error.isEmpty()) {
                        errors.append(result.error);
                        continue;
                    }
                    imported.append(result.media);
                    if (thumbnails.size() >= 256)
                        thumbnails.clear();
                    if (!result.image.isNull())
                        thumbnails.insert(result.media.id, result.image);
                }
                if (!imported.isEmpty())
                    mutate(track < 0 ? "Drop media files" : "Drop files on timeline", [&](Project &p) {
                        QHash<int, qint64> cursors;
                        for (auto media : imported) {
                            for (const auto &existing : p.media)
                                if (QFileInfo(existing.path).absoluteFilePath() ==
                                    QFileInfo(media.path).absoluteFilePath()) {
                                    media = existing;
                                    break;
                                }
                            bool found = false;
                            for (const auto &existing : p.media)
                                found |= existing.id == media.id;
                            if (!found)
                                p.media.append(media);
                            if (track >= 0) {
                                int target = track;
                                auto compatible = [&](int t) {
                                    return p.tracks[t].kind == "video" ? media.video : media.audio;
                                };
                                if (target >= p.tracks.size() || p.tracks[target].locked)
                                    throw Error("Invalid or locked track");
                                if (!compatible(target)) {
                                    target = -1;
                                    for (int i = 0; i < p.tracks.size(); ++i)
                                        if (!p.tracks[i].locked && compatible(i)) {
                                            target = i;
                                            break;
                                        }
                                    if (target < 0)
                                        throw Error("No compatible unlocked track");
                                }
                                auto start = cursors.value(target, frame);
                                auto duration = p.rate.frames(media.durationUs);
                                selection = uuid();
                                p.clips.append({selection, media.id, target, start, 0, duration});
                                cursors[target] = start + duration;
                            }
                        }
                    });
                if (!errors.isEmpty())
                    statusBar()->showMessage("Import failed: " + errors.join("; "), 15000);
                else
                    statusBar()->showMessage(
                        track < 0 ? "Dropped media imported" : "Dropped clips added to timeline", 4000);
            });
    watcher->setFuture(QtConcurrent::run([paths] {
        QVector<Analysis> results;
        for (const auto &path : paths) {
            Analysis result;
            try {
                result.media = probe(path);
                if (result.media.video)
                    result.image = thumbnail(path);
            } catch (const std::exception &e) {
                result.error = QString::fromUtf8(e.what());
            }
            results.append(result);
        }
        return results;
    }));
}
void Editor::sourceSelected() {
    auto *i = pool->currentItem();
    if (!i)
        return;
    try {
        auto m = project.source(i->data(Qt::UserRole).toString());
        metadata->setText(QString("%1\n%2 × %3 · %4 s\nVideo: %5 · Audio: %6")
                              .arg(m.path)
                              .arg(m.width)
                              .arg(m.height)
                              .arg(m.durationUs / 1000000.0, 0, 'f', 3)
                              .arg(m.video ? "yes" : "no", m.audio ? "yes" : "no"));
        timelinePlayback = false;
        pendingPreviewFrame = -1;
        const auto url = QUrl::fromLocalFile(m.path);
        if (player.source() != url) {
            player.stop();
            player.setSource(url);
        }
        previewLabel->setText("SOURCE · " + QFileInfo(m.path).fileName());
    } catch (const Error &e) {
        error(e.what());
    }
}
void Editor::addClip() {
    auto *i = pool->currentItem();
    if (!i) {
        statusBar()->showMessage("Select imported media first", 4000);
        return;
    }
    auto id = i->data(Qt::UserRole).toString();
    mutate("Add clip", [&](Project &p) {
        auto m = p.source(id);
        int t = clipTrack->currentIndex();
        if (t < 0 || (p.tracks[t].kind == "video" && !m.video) || (p.tracks[t].kind == "audio" && !m.audio))
            t = m.video ? 0 : 2;
        if (p.tracks[t].locked)
            throw Error("Track is locked");
        qint64 start = 0;
        for (auto &c : p.clips)
            if (c.track == t)
                start = std::max(start, c.start + c.duration);
        selection = uuid();
        p.clips.append({selection, id, t, start, 0, p.rate.frames(m.durationUs)});
    });
}
void Editor::split() {
    mutate("Split clip", [&](Project &p) { selection = p.split(selection, timeline->playhead()); });
}
void Editor::removeClip() {
    mutate("Delete clip", [&](Project &p) { p.erase(selection); });
    selection.clear();
}
QString Editor::recoveryPath() const {
    auto dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + "/recovery.ffg";
}
void Editor::writeRecovery() {
    if (history.isClean())
        return;
    try {
        project.save(recoveryPath());
        statusBar()->showMessage("Recovery snapshot saved", 2000);
    } catch (const Error &e) {
        statusBar()->showMessage("Autosave failed: " + QString::fromUtf8(e.what()), 10000);
    }
}
bool Editor::canDiscard() {
    if (history.isClean())
        return true;
    auto answer = QMessageBox::warning(this, "Unsaved project", "Save changes before continuing?",
                                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        save();
        return history.isClean();
    }
    return true;
}
void Editor::save(bool as) {
    QString path = projectPath;
    if (path.isEmpty() || as)
        path = QFileDialog::getSaveFileName(this, "Save project", path.isEmpty() ? "Untitled.ffg" : path,
                                            "Frameforge (*.ffg)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".ffg"))
        path += ".ffg";
    try {
        for (const auto &m : project.media)
            if (QFileInfo(m.path).canonicalFilePath() == QFileInfo(path).canonicalFilePath() &&
                QFileInfo(path).exists())
                throw Error("Project cannot overwrite source media");
        project.save(path);
        projectPath = path;
        history.setClean();
        QFile::remove(recoveryPath());
        refresh();
    } catch (const Error &e) {
        error(e.what());
    }
}
void Editor::open() {
    if (render.busy()) {
        error("Finish or cancel rendering before opening a project");
        return;
    }
    auto path = QFileDialog::getOpenFileName(this, "Open project", {}, "Frameforge (*.ffg)");
    if (path.isEmpty())
        return;
    try {
        auto loaded = Project::load(path);
        if (!canDiscard())
            return;
        ++epoch;
        selection.clear();
        projectPath = path;
        history.clear();
        player.stop();
        player.setSource({});
        apply(loaded);
        history.setClean();
    } catch (const Error &e) {
        error(e.what());
    }
}
void Editor::newProject() {
    if (render.busy()) {
        error("Finish or cancel rendering first");
        return;
    }
    if (!canDiscard())
        return;
    ++epoch;
    projectPath.clear();
    selection.clear();
    history.clear();
    player.stop();
    player.setSource({});
    apply(Project{});
    history.setClean();
    QFile::remove(recoveryPath());
}
void Editor::recover() {
    if (render.busy()) {
        error("Finish or cancel rendering before recovery");
        return;
    }
    try {
        auto recovered = Project::load(recoveryPath());
        if (!canDiscard())
            return;
        ++epoch;
        projectPath.clear();
        selection.clear();
        history.clear();
        history.push(new Snapshot(this, project, recovered, "Recover autosave"));
    } catch (const Error &e) {
        error(e.what());
    }
}
void Editor::settings() {
    if (!project.media.isEmpty() || !project.clips.isEmpty()) {
        error("Set resolution and frame rate before importing media into a new project");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Project settings");
    QFormLayout form(&dialog);
    QComboBox size, rate;
    size.addItem("1280 × 720", QSize(1280, 720));
    size.addItem("1920 × 1080", QSize(1920, 1080));
    size.addItem("640 × 360", QSize(640, 360));
    for (auto n : {24, 25, 30, 50, 60})
        rate.addItem(QString::number(n), n);
    rate.setCurrentText("25");
    form.addRow("Resolution", &size);
    form.addRow("Frames per second", &rate);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        mutate("Project settings", [&](Project &p) {
            auto s = size.currentData().toSize();
            p.width = s.width();
            p.height = s.height();
            p.rate = {rate.currentData().toInt(), 1};
        });
}
void Editor::playSource() {
    pendingPreviewFrame = -1;
    if (!timelinePlayback && player.playbackState() == QMediaPlayer::PlayingState) {
        player.pause();
        return;
    }
    if (timelinePlayback || player.source().isEmpty()) {
        if (!pool->currentItem()) {
            statusBar()->showMessage("Select imported media first", 5000);
            return;
        }
        sourceSelected();
    }
    if (player.source().isEmpty()) {
        statusBar()->showMessage("Select imported media first", 5000);
        return;
    }
    player.play();
}
void Editor::playTimeline() {
    if (project.clips.isEmpty()) {
        statusBar()->showMessage(
            "Timeline is empty. Drop media onto a track or use Add clip. Use Play source to browse footage.",
            10000);
        return;
    }
    if (timelinePlayback && player.playbackState() == QMediaPlayer::PlayingState) {
        player.pause();
        return;
    }
    if (timelinePlayback && previewValid && player.source() == QUrl::fromLocalFile(previewPath) &&
        player.mediaStatus() != QMediaPlayer::EndOfMedia) {
        player.play();
        return;
    }
    renderTimeline(false);
}
void Editor::loadTimelinePreview() {
    const auto requested = std::max(qint64(0), pendingPreviewFrame);
    timelinePlayback = false;
    player.stop();
    pendingPreviewFrame = requested;
    timelinePlayback = true;
    previewLabel->setText("TIMELINE PREVIEW · matches rendered export");
    player.setSource(QUrl::fromLocalFile(previewPath));
    const auto status = player.mediaStatus();
    if (pendingPreviewFrame >= 0 &&
        (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
        const auto frame = pendingPreviewFrame;
        pendingPreviewFrame = -1;
        player.setPosition(project.rate.us(frame) / 1000);
    }
    player.play();
}
void Editor::renderTimeline(bool exporting) {
    if (render.busy()) {
        statusBar()->showMessage("A render is already running", 4000);
        return;
    }
    if (codec->currentText().isEmpty()) {
        error("No supported FFmpeg video/AAC encoder available");
        return;
    }
    QString out;
    if (exporting) {
        out = QFileDialog::getSaveFileName(this, "Export to a NEW file", "output.mp4", "MP4 (*.mp4)");
        if (out.isEmpty())
            return;
        if (!out.endsWith(".mp4"))
            out += ".mp4";
    } else {
        pendingPreviewFrame = timeline->playhead() < project.end() ? timeline->playhead() : 0;
        if (previewValid) {
            loadTimelinePreview();
            return;
        }
        timelinePlayback = false;
        player.pause();
        previewLabel->setText("Preparing timeline preview…");
        if (!previewDirectory.isValid()) {
            error("No writable preview cache directory");
            return;
        }
        if (!previewPath.isEmpty()) {
            player.stop();
            player.setSource({});
            QFile::remove(previewPath);
            previewPath.clear();
        }
        out = previewDirectory.filePath(uuid() + ".mp4");
    }
    try {
        previewRender = !exporting;
        render.start(project, out, codec->currentText());
        statusBar()->showMessage("Background CPU render started");
    } catch (const Error &e) {
        error(e.what());
    }
}
void Editor::seekFrame(qint64 f) {
    player.pause();
    timeline->setPlayhead(f);
    if (timelinePlayback && previewValid) {
        player.setPosition(project.rate.us(f) / 1000);
    } else
        statusBar()->showMessage(QString("Playhead: frame %1. Press Play timeline to view edits.").arg(f),
                                 4000);
}
void Editor::closeEvent(QCloseEvent *e) {
    if (render.busy()) {
        if (QMessageBox::question(this, "Render running", "Cancel render and close?") != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        render.cancel();
    }
    if (!canDiscard()) {
        e->ignore();
        return;
    }
    QSettings().setValue("layout", saveState());
    QFile::remove(recoveryPath());
    e->accept();
}
} // namespace ff
