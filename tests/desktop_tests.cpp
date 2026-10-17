// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/media.h"
#include "ui/editor.h"
#include "ui/media_pool.h"
#include "ui/theme.h"
#include "ui/timeline.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QtTest>
namespace ff {
class EditorTests : public QObject {
    Q_OBJECT
  private slots:
    void timelineTransport() {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        auto source = dir.filePath("playback.mp4");
        QProcess gen;
        gen.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=s=160x90:r=25:d=3", "-c:v",
                             "mpeg4", source});
        QVERIFY(gen.waitForFinished());
        QCOMPARE(gen.exitCode(), 0);
        Editor e;
        e.show();
        QTRY_VERIFY_WITH_TIMEOUT(e.codec->count() > 0, 10000);
        e.importPath(source);
        QTRY_COMPARE_WITH_TIMEOUT(e.project.media.size(), 1, 15000);
        e.addClip();
        e.timeline->setPlayhead(15);
        QPushButton *play = nullptr;
        for (auto *b : e.findChildren<QPushButton *>())
            if (b->text() == "Play / Pause" || b->objectName() == "playTimeline")
                play = b;
        QVERIFY(play);
        QTest::mouseClick(play, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(e.timelinePlayback, 15000);
        QVERIFY(!e.render.busy());
        QTRY_VERIFY_WITH_TIMEOUT(e.timeline->playhead() > 20, 10000);
        QTest::mouseClick(play, Qt::LeftButton);
        QVERIFY(!e.timelinePlayer.isPlaying());
        auto frame = e.timeline->playhead();
        QTest::qWait(150);
        QVERIFY(qAbs(e.timeline->playhead() - frame) <= 1);
        e.seekFrame(30);
        QCOMPARE(e.timeline->playhead(), qint64(30));
        QTest::mouseClick(play, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(e.timeline->playhead() > 35, 10000);
        e.timelinePlayer.pause();
        e.apply(e.project);
        QTest::mouseClick(play, Qt::LeftButton);
        e.playSource();
        QTRY_VERIFY_WITH_TIMEOUT(!e.render.busy(), 15000);
        QVERIFY(!e.timelinePlayback);
        QCOMPARE(e.player.source(), QUrl::fromLocalFile(source));
        e.player.pause();
        e.history.setClean();
        QFile::remove(e.recoveryPath());
    }
    void externalTimelineDrop() {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        auto source = dir.filePath("drop ü clip.mp4");
        QProcess gen;
        gen.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=160x90:r=25:d=1", "-c:v",
                             "mpeg4", source});
        QVERIFY(gen.waitForFinished());
        QCOMPARE(gen.exitCode(), 0);
        Editor e;
        e.show();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(source)});
        QPoint target(200, 65);
        QDragEnterEvent enter(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e.timeline, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(target, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        drop.setDropAction(Qt::MoveAction);
        QApplication::sendEvent(e.timeline, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(drop.dropAction(), Qt::CopyAction);
        QTRY_COMPARE_WITH_TIMEOUT(e.project.clips.size(), 1, 15000);
        QCOMPARE(e.project.media.size(), 1);
        QCOMPARE(e.project.clips[0].track, 0);
        QCOMPARE(e.project.clips[0].start, qint64(10));
        e.history.undo();
        QCOMPARE(e.project.clips.size(), 0);
        QCOMPARE(e.project.media.size(), 0);
        e.history.redo();
        QCOMPARE(e.project.clips.size(), 1);
        std::unique_ptr<QMimeData> internal(e.pool->mimeData({e.pool->item(0)}));
        QCOMPARE(internal->data("application/x-frameforge-media-id"), e.project.media[0].id.toUtf8());
        QCOMPARE(internal->urls().first().toLocalFile(), source);
        QPoint second(400, 130);
        QDragEnterEvent enter2(second, Qt::CopyAction, internal.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e.timeline, &enter2);
        QVERIFY(enter2.isAccepted());
        QDropEvent drop2(second, Qt::CopyAction, internal.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e.timeline, &drop2);
        QTRY_COMPARE(e.project.clips.size(), 2);
        QCOMPARE(e.project.media.size(), 1);
        QCOMPARE(e.project.clips.last().track, 1);
        QVERIFY(QFile::exists(source));
        QMimeData remote;
        remote.setUrls({QUrl("https://example.org/video.mp4")});
        QDragEnterEvent bad(target, Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e.timeline, &bad);
        QVERIFY(!bad.isAccepted());
        e.history.setClean();
        QFile::remove(e.recoveryPath());
    }
    void poolAndBatchDrops() {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        auto source = dir.filePath("one.mp4"), second = dir.filePath("two.mp4");
        QProcess gen;
        gen.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=c=red:s=160x90:r=25:d=1", "-c:v",
                             "mpeg4", source});
        QVERIFY(gen.waitForFinished());
        QCOMPARE(gen.exitCode(), 0);
        QVERIFY(QFile::copy(source, second));
        Editor e;
        e.show();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(source), QUrl::fromLocalFile(second)});
        auto *viewport = e.pool->viewport();
        QPoint point(20, 20);
        QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &drop);
        QVERIFY(drop.isAccepted());
        QTRY_COMPARE_WITH_TIMEOUT(e.project.media.size(), 2, 15000);
        QVERIFY(e.project.clips.isEmpty());
        QPoint timelinePoint(160, 65);
        QDragEnterEvent next(timelinePoint, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e.timeline, &next);
        QDropEvent clips(timelinePoint, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e.timeline, &clips);
        QTRY_COMPARE_WITH_TIMEOUT(e.project.clips.size(), 2, 15000);
        QCOMPARE(e.project.media.size(), 2);
        QCOMPARE(e.project.source(e.project.clips[0].media).path, source);
        QCOMPARE(e.project.clips[1].start, qint64(25));
        e.history.undo();
        QVERIFY(e.project.clips.isEmpty());
        QCOMPARE(e.project.media.size(), 2);
        QDragEnterEvent rootEnter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&e, &rootEnter);
        QVERIFY(rootEnter.isAccepted());
        QDropEvent moveOnly(timelinePoint, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e.timeline, &moveOnly);
        QVERIFY(!moveOnly.isAccepted());
        QVERIFY(QFile::exists(source));
        QVERIFY(QFile::exists(second));
        e.history.setClean();
        QFile::remove(e.recoveryPath());
    }
    void sourcePlaybackDoesNotMoveTimeline() {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        auto path = dir.filePath("source.mp4");
        QProcess gen;
        gen.start("ffmpeg",
                  {"-v", "error", "-f", "lavfi", "-i", "testsrc2=s=160x90:r=25:d=2", "-c:v", "mpeg4", path});
        QVERIFY(gen.waitForFinished());
        QCOMPARE(gen.exitCode(), 0);
        for (int repeat = 0; repeat < 3; ++repeat) {
            Editor e;
            e.show();
            e.importPath(path);
            QTRY_COMPARE_WITH_TIMEOUT(e.project.media.size(), 1, 15000);
            e.playTimeline();
            QVERIFY(e.player.playbackState() != QMediaPlayer::PlayingState);
            QVERIFY(e.statusBar()->currentMessage().contains("empty", Qt::CaseInsensitive));
            e.addClip();
            e.timeline->setPlayhead(12);
            auto *source = e.findChild<QPushButton *>("playSource");
            QVERIFY(source);
            QTest::mouseClick(source, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(e.player.position() > 200, 10000);
            QVERIFY(!e.timelinePlayback);
            QCOMPARE(e.timeline->playhead(), qint64(12));
            const auto itemPoint = e.pool->visualItemRect(e.pool->item(0)).center();
            QTest::mouseClick(e.pool->viewport(), Qt::LeftButton, Qt::NoModifier, itemPoint);
            QTest::mouseDClick(e.pool->viewport(), Qt::LeftButton, Qt::NoModifier, itemPoint);
            QTRY_COMPARE(e.project.clips.size(), 2);
            QTest::mousePress(e.timeline, Qt::LeftButton, Qt::NoModifier, QPoint(380, 70));
            QTest::mouseMove(e.timeline, QPoint(420, 140));
            QTest::mouseRelease(e.timeline, Qt::LeftButton, Qt::NoModifier, QPoint(420, 140));
            QTRY_COMPARE(e.project.clips.last().track, 1);
            QVERIFY(e.player.playbackState() == QMediaPlayer::PlayingState);
            QTest::mouseClick(source, Qt::LeftButton);
            QCOMPARE(e.player.playbackState(), QMediaPlayer::PausedState);
            e.sourceSelected();
            e.sourceSelected();
            e.history.setClean();
            QFile::remove(e.recoveryPath());
        }
    }
    void editAndPreview() {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        QString source = dir.filePath("source.mp4");
        QProcess gen;
        gen.start("ffmpeg",
                  {"-v", "error", "-f", "lavfi", "-i", "testsrc2=s=160x90:r=25:d=2", "-f", "lavfi", "-i",
                   "sine=frequency=440:duration=2", "-c:v", "mpeg4", "-c:a", "aac", "-shortest", source});
        QVERIFY(gen.waitForFinished());
        QCOMPARE(gen.exitCode(), 0);
        Editor e;
        e.resize(1280, 800);
        e.show();
        QTRY_VERIFY_WITH_TIMEOUT(e.codec->count() > 0, 10000);
        e.importPath(source);
        QTRY_COMPARE_WITH_TIMEOUT(e.project.media.size(), 1, 15000);
        QCOMPARE(e.pool->count(), 1);
        auto *video = e.findChild<QVideoWidget *>();
        QVERIFY(video);
        QSignalSpy frames(video->videoSink(), &QVideoSink::videoFrameChanged);
        QTRY_VERIFY_WITH_TIMEOUT(e.player.mediaStatus() == QMediaPlayer::LoadedMedia ||
                                     e.player.mediaStatus() == QMediaPlayer::BufferedMedia,
                                 15000);
        e.player.play();
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 10000);
        QVERIFY(!video->videoSink()->videoFrame().toImage().isNull());
        e.player.pause();
        e.addClip();
        QCOMPARE(e.project.clips.size(), 1);
        e.timeline->setPlayhead(20);
        e.split();
        QCOMPARE(e.project.clips.size(), 2);
        // Xvfb has no window manager; activate Qt shortcut dispatch explicitly.
        QApplication::setActiveWindow(&e);
        e.setFocus();
        QTest::keySequence(&e, QKeySequence::Undo);
        QTRY_COMPARE(e.project.clips.size(), 1);
        QTest::keySequence(&e, QKeySequence::Redo);
        QTRY_COMPARE(e.project.clips.size(), 2);
        e.mutate("Move split clip", [&](Project &p) { p.move(e.selection, 1, 30); });
        QCOMPARE(e.project.clip(e.selection).track, 1);
        auto mediaId = e.project.media[0].id;
        auto relocated = dir.filePath("relocated.mp4");
        e.player.stop();
        e.player.setSource({});
        QVERIFY(QFile::rename(source, relocated));
        e.importPath(relocated, mediaId);
        QTRY_COMPARE_WITH_TIMEOUT(e.project.media[0].path, relocated, 15000);
        QCOMPARE(e.project.clips[0].media, mediaId);
        e.projectPath = dir.filePath("project.ffg");
        e.save();
        QVERIFY(e.history.isClean());
        e.apply(Project::load(e.projectPath));
        QCOMPARE(e.project.clips.size(), 2);
        e.mutate("Trim", [&](Project &p) { p.trim(e.selection, 25, 20); });
        e.writeRecovery();
        QCOMPARE(Project::load(e.recoveryPath()).clip(e.selection).duration, 20);
        QSignalSpy rendered(&e.render, &RenderJob::finished);
        QSignalSpy live(&e.timelinePlayer, &TimelinePlayer::frameReady);
        e.playTimeline();
        QTRY_VERIFY_WITH_TIMEOUT(!live.isEmpty(), 5000);
        QVERIFY(e.timelinePlayback);
        QVERIFY(!e.render.busy());
        QVERIFY(rendered.isEmpty());
        QVERIFY(!qvariant_cast<QImage>(live.last()[0]).isNull());
        e.timelinePlayer.pause();
        e.seekFrame(35);
        QCOMPARE(e.timeline->playhead(), qint64(35));
        if (QGuiApplication::platformName() == "xcb") {
            QTest::qWait(100);
            QVERIFY(e.screen()->grabWindow(e.winId()).save("desktop-x11.png"));
        } else {
            QVERIFY(e.grab().save("desktop-preview.png"));
        }
        e.history.setClean();
        QFile::remove(e.recoveryPath());
    }
    void splitShortcutsAndTyping() {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        const auto path = dir.filePath("split.mp4");
        QProcess gen;
        gen.start("ffmpeg",
                  {"-v", "error", "-f", "lavfi", "-i", "color=red:s=160x90:r=25:d=4", "-c:v", "mpeg4", path});
        QVERIFY(gen.waitForFinished());
        QCOMPARE(gen.exitCode(), 0);
        Editor e;
        e.show();
        e.importPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(e.project.media.size(), 1, 15000);
        e.addClip();
        QApplication::setActiveWindow(&e);
        e.timeline->setFocus();
        e.selection.clear();
        e.timeline->setPlayhead(25);
        QTest::keyClick(e.timeline, Qt::Key_S);
        QTRY_COMPARE(e.project.clips.size(), 2);
        QVERIFY(!e.render.busy());
        e.timeline->setPlayhead(50);
        QTest::keySequence(e.timeline, QKeySequence("Ctrl+B"));
        QTRY_COMPARE(e.project.clips.size(), 3);
        e.history.undo();
        QCOMPARE(e.project.clips.size(), 2);
        e.history.redo();
        QCOMPARE(e.project.clips.size(), 3);
        auto *text = e.findChild<QLineEdit *>();
        QVERIFY(text);
        text->setFocus();
        QTest::keyClicks(text, "sss");
        QVERIFY(text->text().endsWith("sss"));
        QCOMPARE(e.project.clips.size(), 3);
        text->clear();
        e.timeline->setFocus();
        // Boundary and locked-track splits do not open a modal dialog.
        e.split();
        QCOMPARE(e.project.clips.size(), 3);
        e.mutate("Lock", [](Project &p) { p.tracks[0].locked = true; });
        e.timeline->setPlayhead(65);
        QTest::keyClick(e.timeline, Qt::Key_S);
        QCOMPARE(e.project.clips.size(), 3);
        QVERIFY(!e.statusBar()->currentMessage().isEmpty());
        e.history.setClean();
        QFile::remove(e.recoveryPath());
    }
};
} // namespace ff
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("FrameforgeTests");
    app.setApplicationName("FrameforgeTests");
    ff::applyTheme(app);
    ff::EditorTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "desktop_tests.moc"
