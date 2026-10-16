// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/media.h"
#include "ui/editor.h"
#include "ui/theme.h"
#include "ui/timeline.h"
#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QScreen>
#include <QTemporaryDir>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QtTest>
namespace ff {
class EditorTests : public QObject {
    Q_OBJECT
  private slots:
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
        e.renderTimeline(false);
        QVERIFY(rendered.wait(30000));
        QVERIFY2(rendered[0][0].toBool(), qPrintable(rendered[0][1].toString()));
        QVERIFY(e.timelinePlayback);
        QVERIFY(e.previewValid);
        QVERIFY(probe(e.previewPath).video);
        frames.clear();
        e.player.play();
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 10000);
        QVERIFY(!video->videoSink()->videoFrame().toImage().isNull());
        e.player.pause();
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
