// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/media.h"
#include "media/timeline_decoder.h"
#include "media/timeline_player.h"
#include "media/render.h"
#include <QFile>
#include <QProcess>
#include <QMediaDevices>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <cstring>
using namespace ff;
class PlaybackTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    Media source;
    Project fixture() {
        Project p;
        p.width = 160;
        p.height = 90;
        p.media = {source};
        return p;
    }
    static double rms(const QByteArray &pcm) {
        double sum = 0;
        for (qsizetype i = 0; i + 4 <= pcm.size(); i += 4) {
            float v;
            std::memcpy(&v, pcm.constData() + i, 4);
            sum += v * v;
        }
        return pcm.isEmpty() ? 0 : std::sqrt(sum / (pcm.size() / 4));
    }
  private slots:
    void initTestCase() {
        const auto path = dir.filePath("red then blue ü.mkv");
        QProcess gen;
        gen.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=red:s=160x90:r=25:d=2",
                  "-f", "lavfi", "-i", "color=blue:s=160x90:r=25:d=2", "-f", "lavfi", "-i",
                  "sine=frequency=440:sample_rate=48000:duration=4", "-filter_complex",
                  "[0:v][1:v]concat=n=2:v=1:a=0[v]", "-map", "[v]", "-map", "2:a",
                  "-c:v", "ffv1", "-c:a", "pcm_f32le", path});
        QVERIFY(gen.waitForFinished(15000));
        QVERIFY2(gen.exitCode() == 0, gen.readAllStandardError().constData());
        source = probe(path);
    }
    void trimmedLayersAndGaps() {
        auto p = fixture();
        p.clips = {{"bottom", source.id, 0, 10, 0, 75}, {"top", source.id, 1, 20, 50, 25}};
        TimelineDecoder decoder(p, {160, 90});
        auto gap = decoder.video(0);
        QVERIFY(!gap.isNull());
        QVERIFY(gap.pixelColor(80, 45).red() < 10);
        QVERIFY(decoder.video(15).pixelColor(80, 45).red() > 220);
        QVERIFY(decoder.video(25).pixelColor(80, 45).blue() > 220);
        QVERIFY(decoder.video(45).pixelColor(80, 45).red() > 220);
        QVERIFY(decoder.video(80).pixelColor(80, 45).blue() > 220);
        QVERIFY(decoder.video(90).pixelColor(80, 45).blue() < 10);
        p.tracks[1].muted = true;
        TimelineDecoder muted(p, {160, 90});
        QVERIFY(muted.video(25).pixelColor(80, 45).red() > 220);
        p.tracks[1].muted = false;
        p.tracks[0].solo = true;
        TimelineDecoder solo(p, {160, 90});
        QVERIFY(solo.video(25).pixelColor(80, 45).red() > 220);
    }
    void audioTrimSplitMixAndSilence() {
        auto p = fixture();
        p.clips = {{"one", source.id, 0, 5, 10, 50}};
        TimelineDecoder one(p, {160, 90});
        QCOMPARE(one.audio(0, 4800).size(), 4800 * 2 * 4);
        QVERIFY(rms(one.audio(0, 4800)) < 0.00001);
        const auto baseline = one.audio(24000, 4800);
        QVERIFY(rms(baseline) > 0.04);
        double phaseError = 0;
        for (int i = 0; i < 4800; ++i) {
            float actual;
            std::memcpy(&actual, baseline.constData() + i * 8, 4);
            const double expected = std::sin(2 * 3.141592653589793 * 440 * (33600 + i) / 48000.0) *
                                    (4095.0 / 32768) / std::sqrt(2.0);
            phaseError = std::max(phaseError, std::abs(actual - expected));
        }
        QVERIFY2(phaseError < 0.001, qPrintable(QString::number(phaseError)));
        auto split = p;
        split.split("one", 25);
        TimelineDecoder divided(split, {160, 90});
        const auto intact = one.audio(47000, 3000), cut = divided.audio(47000, 3000);
        QCOMPARE(cut.size(), intact.size());
        double maximumDifference = 0;
        for (qsizetype i = 0; i < cut.size(); i += 4) {
            float a, b;
            std::memcpy(&a, cut.constData() + i, 4);
            std::memcpy(&b, intact.constData() + i, 4);
            maximumDifference = std::max(maximumDifference, double(std::abs(a - b)));
        }
        QVERIFY2(maximumDifference < 0.003, qPrintable(QString::number(maximumDifference)));
        p.clips.append({"two", source.id, 2, 5, 10, 50});
        TimelineDecoder mixed(p, {160, 90});
        QVERIFY(std::abs(rms(mixed.audio(24000, 4800)) / rms(baseline) - 2) < 0.02);
        p.tracks[0].muted = true;
        p.tracks[2].muted = true;
        TimelineDecoder silent(p, {160, 90});
        QCOMPARE(rms(silent.audio(24000, 4800)), 0.0);
    }
    void seekLongTimelineAndFailures() {
        auto p = fixture();
        p.clips = {{"far", source.id, 0, 900000, 50, 25}};
        QElapsedTimer timer;
        timer.start();
        TimelineDecoder decoder(p, {160, 90});
        QVERIFY(decoder.video(900010).pixelColor(80, 45).blue() > 220);
        QVERIFY(timer.elapsed() < 3000);
        bool cancel = true;
        TimelineDecoder cancelled(p, {160, 90}, [&] { return cancel; });
        QVERIFY_EXCEPTION_THROWN(cancelled.video(900010), Error);
        p.media[0].path = dir.filePath("missing.mkv");
        TimelineDecoder missing(p, {160, 90});
        QVERIFY_EXCEPTION_THROWN(missing.video(900010), Error);
        QFile bad(dir.filePath("bad.mkv"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("broken");
        bad.close();
        p.media[0].path = bad.fileName();
        TimelineDecoder corrupt(p, {160, 90});
        QVERIFY_EXCEPTION_THROWN(corrupt.video(900010), Error);
    }
    void transportWithoutRender() {
        auto p = fixture();
        p.clips = {{"whole", source.id, 0, 0, 0, 100}, {"far", source.id, 0, 900000, 0, 25}};
        TimelinePlayer player;
        player.setAudioDevice({}); // Explicitly exercise silent/absent-device playback.
        player.setProject(p);
        QSignalSpy frames(&player, &TimelinePlayer::frameReady);
        QSignalSpy errors(&player, &TimelinePlayer::error);
        QElapsedTimer elapsed;
        elapsed.start();
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.frame() >= 5, 3000);
        QVERIFY(elapsed.elapsed() < 3000);
        QVERIFY(!frames.isEmpty());
        player.pause();
        const auto paused = player.frame();
        QTest::qWait(150);
        QCOMPARE(player.frame(), paused);
        player.seek(70);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty() &&
            qvariant_cast<QImage>(frames.last()[0]).pixelColor(80, 45).blue() > 220, 3000);
        QCOMPARE(player.frame(), qint64(70));
        frames.clear();
        player.seek(55);
        player.seek(10);
        player.seek(75);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
        QCOMPARE(player.frame(), qint64(75));
        QVERIFY(qvariant_cast<QImage>(frames.last()[0]).pixelColor(80, 45).blue() > 220);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.frame() > 78, 3000);
        player.pause();
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors[0][0].toString()));
        player.seek(p.end() - 2);
        QSignalSpy ended(&player, &TimelinePlayer::ended);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty(), 3000);
        QCOMPARE(player.frame(), p.end());
        QVERIFY(!player.isPlaying());
        QVERIFY(QDir(dir.path()).entryList({"*.mp4", "*.part*"}, QDir::Files).isEmpty());
    }
    void changingProjectAndTeardown() {
        auto p = fixture();
        p.clips = {{"whole", source.id, 0, 0, 0, 100}};
        for (int repeat = 0; repeat < 5; ++repeat) {
            TimelinePlayer player;
            player.setAudioDevice({});
            player.setProject(p);
            player.play();
            player.seek(80);
            auto changed = p;
            changed.clips[0].in = 50;
            changed.clips[0].duration = 40;
            player.setProject(changed);
            player.seek(5);
            QSignalSpy frames(&player, &TimelinePlayer::frameReady);
            QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
            QVERIFY(!player.isPlaying());
            QCOMPARE(player.frame(), qint64(5));
            QVERIFY(qvariant_cast<QImage>(frames.last()[0]).pixelColor(80, 45).blue() > 220);
            player.play();
        }
    }
    void variableTimestampsAndOffset() {
        auto p = fixture();
        const auto variable = dir.filePath("variable.mkv");
        QProcess gen;
        gen.start("ffmpeg", {"-v", "error", "-i", source.path, "-an", "-vf",
            "setpts=if(lt(N\\,50)\\,N/(10*TB)\\,(5+(N-50)/50)/TB)",
            "-fps_mode", "vfr", "-c:v", "ffv1", "-output_ts_offset", "7", variable});
        QVERIFY(gen.waitForFinished(10000));
        QVERIFY2(gen.exitCode() == 0, gen.readAllStandardError().constData());
        p.media = {probe(variable)};
        p.clips = {{"vfr", p.media[0].id, 0, 0, 0, 150}};
        TimelineDecoder decoder(p, {160, 90});
        QVERIFY(decoder.video(100).pixelColor(80, 45).red() > 220);
        QVERIFY(decoder.video(130).pixelColor(80, 45).blue() > 220);
        QVERIFY(decoder.video(10).pixelColor(80, 45).red() > 220);
    }
    void fractionalRateSeek() {
        auto p = fixture();
        p.rate = {30000, 1001};
        p.clips = {{"whole", source.id, 0, 61, 60, 40}};
        TimelinePlayer player;
        player.setAudioDevice({});
        player.setProject(p);
        QSignalSpy frames(&player, &TimelinePlayer::frameReady);
        // Rounded sample boundaries must not seek to frame 60 (the preceding gap).
        player.seek(61);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
        QVERIFY(qvariant_cast<QImage>(frames.last()[0]).pixelColor(80, 45).blue() > 220);
        QCOMPARE(player.frame(), qint64(61));
    }
    void fractionalAudioSplitContinuity() {
        auto p = fixture();
        p.rate = {30000, 1001};
        p.clips = {{"whole", source.id, 0, 1, 0, 90}};
        auto split = p;
        split.split("whole", 35);
        TimelineDecoder original(p, {160, 90}), divided(split, {160, 90});
        const auto start = 35LL * 48000 * 1001 / 30000 - 1000;
        const auto a = original.audio(start, 3000), b = divided.audio(start, 3000);
        QCOMPARE(a.size(), b.size());
        double difference = 0;
        for (int i = 0; i < 3000 * 2; ++i) {
            float left, right;
            std::memcpy(&left, a.constData() + i * 4, 4);
            std::memcpy(&right, b.constData() + i * 4, 4);
            difference = std::max(difference, double(std::abs(left - right)));
        }
        QVERIFY2(difference < 0.0001, qPrintable(QString::number(difference)));
    }
    void previewMatchesExport() {
        QTemporaryDir out;
        auto p = fixture();
        p.clips = {{"bottom", source.id, 0, 0, 0, 50}, {"top", source.id, 1, 10, 50, 20},
                   {"audio", source.id, 2, 5, 10, 40}};
        RenderJob job;
        QSignalSpy rendered(&job, &RenderJob::finished);
        const auto path = out.filePath("reference.mp4");
        job.start(p, path);
        QVERIFY(rendered.wait(15000));
        QVERIFY2(rendered[0][0].toBool(), qPrintable(rendered[0][1].toString()));
        TimelineDecoder decoder(p, {160, 90});
        for (int frame : {0, 8, 15, 35, 48}) {
            QProcess ffmpeg;
            ffmpeg.start("ffmpeg", {"-v", "error", "-ss", QString::number(frame / 25.0), "-i", path,
                "-frames:v", "1", "-vf", "scale=1:1", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"});
            QVERIFY(ffmpeg.waitForFinished());
            QCOMPARE(ffmpeg.exitCode(), 0);
            const auto pixel = ffmpeg.readAllStandardOutput();
            QCOMPARE(pixel.size(), 3);
            const auto actual = decoder.video(frame).pixelColor(80, 45);
            QVERIFY(std::abs(actual.red() - quint8(pixel[0])) < 15);
            QVERIFY(std::abs(actual.blue() - quint8(pixel[2])) < 15);
        }
        QProcess audio;
        audio.start("ffmpeg", {"-v", "error", "-ss", "0.6", "-i", path, "-t", "0.08", "-vn", "-ar",
            "48000", "-ac", "2", "-f", "f32le", "pipe:1"});
        QVERIFY(audio.waitForFinished());
        QCOMPARE(audio.exitCode(), 0);
        const auto reference = audio.readAllStandardOutput();
        QVERIFY(std::abs(rms(decoder.audio(28800, 3840)) / rms(reference) - 1) < 0.08);
    }
    void audioDevicePlayback() {
        if (!qEnvironmentVariableIsSet("FRAMEFORGE_TEST_AUDIO"))
            QSKIP("Set FRAMEFORGE_TEST_AUDIO=1 with a real or virtual audio output");
        QVERIFY(!QMediaDevices::defaultAudioOutput().isNull());
        auto p = fixture();
        p.clips = {{"whole", source.id, 0, 0, 0, 100}};
        TimelinePlayer player;
        player.setProject(p);
        QSignalSpy errors(&player, &TimelinePlayer::error);
        QElapsedTimer elapsed;
        elapsed.start();
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.frame() >= 15, 3000);
        QVERIFY(elapsed.elapsed() >= 400);
        player.pause();
        const auto stopped = player.frame();
        QTest::qWait(100);
        QCOMPARE(player.frame(), stopped);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.frame() >= stopped + 5, 3000);
        player.seek(80);
        player.play();
        QSignalSpy ended(&player, &TimelinePlayer::ended);
        QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty(), 3000);
        QCOMPARE(player.frame(), qint64(100));
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors[0][0].toString()));
    }
};
QTEST_GUILESS_MAIN(PlaybackTests)
#include "playback_tests.moc"
