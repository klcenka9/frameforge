// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/media.h"
#include "media/render.h"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace ff;
class MediaTests : public QObject {
    Q_OBJECT
  private:
    QByteArray decode(const QString &file, const QString &time, bool audio = false) {
        QProcess p;
        QStringList args{"-v", "error", "-ss", time, "-i", file};
        if (audio)
            args << "-t" << "0.05" << "-vn" << "-ac" << "1" << "-ar" << "48000" << "-f" << "s16le"
                 << "pipe:1";
        else
            args << "-frames:v" << "1" << "-vf" << "scale=1:1" << "-pix_fmt" << "rgb24" << "-f" << "rawvideo"
                 << "pipe:1";
        p.start("ffmpeg", args);
        if (!p.waitForFinished(10000) || p.exitCode())
            return {};
        return p.readAllStandardOutput();
    }
    double energy(const QByteArray &pcm) {
        double sum = 0;
        for (int i = 0; i + 1 < pcm.size(); i += 2) {
            qint16 n = qint16(quint8(pcm[i]) | (quint16(quint8(pcm[i + 1])) << 8));
            sum += double(n) * n;
        }
        return pcm.isEmpty() ? 0 : std::sqrt(sum / (pcm.size() / 2));
    }
  private slots:
    void probeAndRender() {
        QTemporaryDir d;
        QString source = d.filePath("source ' ü.mp4");
        QProcess gen;
        gen.start("ffmpeg",
                  {"-v", "error", "-f", "lavfi", "-i", "color=c=red:s=160x90:r=25:d=2", "-f", "lavfi", "-i",
                   "sine=frequency=440:duration=2", "-c:v", "mpeg4", "-c:a", "aac", "-shortest", source});
        QVERIFY(gen.waitForFinished(15000));
        QCOMPARE(gen.exitCode(), 0);
        auto m = probe(source);
        QVERIFY(m.video);
        QVERIFY(m.audio);
        QVERIFY(m.durationUs >= 1900000);
        QVERIFY_EXCEPTION_THROWN(probe(d.filePath("missing")), Error);
        QFile bad(d.filePath("bad.mov"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("corrupt");
        bad.close();
        QVERIFY_EXCEPTION_THROWN(probe(bad.fileName()), Error);
        Project p;
        p.width = 160;
        p.height = 90;
        p.media.append(m);
        p.clips.append({"c", m.id, 0, 5, 10, 25});
        RenderJob job;
        QSignalSpy finished(&job, &RenderJob::finished);
        QString out = d.filePath("output.mp4");
        job.start(p, out, "mpeg4");
        QVERIFY(finished.wait(30000));
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        auto result = probe(out);
        QVERIFY(result.video);
        QVERIFY(result.audio);
        QVERIFY(qAbs(result.durationUs - 1200000) < 150000);
        QCOMPARE(result.width, 160);
        auto black = decode(out, "0.05");
        QCOMPARE(black.size(), 3);
        QVERIFY(quint8(black[0]) < 12);
        auto red = decode(out, "0.6");
        QCOMPARE(red.size(), 3);
        QVERIFY(quint8(red[0]) > 220);
        QVERIFY(quint8(red[2]) < 30);
        auto gapAudio = decode(out, "0.02", true), toneAudio = decode(out, "0.6", true);
        QVERIFY(!gapAudio.isEmpty());
        QVERIFY(!toneAudio.isEmpty());
        QVERIFY(energy(gapAudio) < 100);
        QVERIFY(energy(toneAudio) > 1000);
        auto bluePath = d.filePath("blue.mp4");
        gen.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=160x90:r=25:d=2", "-c:v",
                             "mpeg4", bluePath});
        QVERIFY(gen.waitForFinished());
        QCOMPARE(gen.exitCode(), 0);
        auto blueMedia = probe(bluePath);
        Project layered;
        layered.width = 160;
        layered.height = 90;
        layered.media = {m, blueMedia};
        layered.clips = {{"bottom", m.id, 0, 0, 0, 50}, {"top", blueMedia.id, 1, 10, 0, 20}};
        QSignalSpy composite(&job, &RenderJob::finished);
        auto compositePath = d.filePath("composite.mp4");
        job.start(layered, compositePath, "mpeg4");
        QVERIFY(composite.wait(30000));
        QVERIFY(composite[0][0].toBool());
        auto blue = decode(compositePath, "0.6");
        QCOMPARE(blue.size(), 3);
        QVERIFY(quint8(blue[2]) > 220);
        auto revealed = decode(compositePath, "1.6");
        QCOMPARE(revealed.size(), 3);
        QVERIFY(quint8(revealed[0]) > 220);

        QVERIFY_EXCEPTION_THROWN(job.start(p, source, "mpeg4"), Error);
        QCOMPARE(probe(source).durationUs, m.durationUs);
        p.tracks[0].muted = true;
        auto args = renderArguments(p, d.filePath("silent.mp4"), "mpeg4");
        QVERIFY(!args.contains(source));
    }
    void cancellation() {
        QTemporaryDir d;
        QString src = d.filePath("clip.mp4");
        QProcess gen;
        gen.start("ffmpeg",
                  {"-v", "error", "-f", "lavfi", "-i", "testsrc2=s=160x90:r=25:d=1", "-c:v", "mpeg4", src});
        QVERIFY(gen.waitForFinished());
        QCOMPARE(gen.exitCode(), 0);
        Project p;
        p.media.append(probe(src));
        p.clips.append({"c", p.media[0].id, 0, 100000, 0, 25});
        RenderJob job;
        QSignalSpy finished(&job, &RenderJob::finished);
        auto out = d.filePath("cancel.mp4");
        job.start(p, out, "mpeg4");
        job.cancel();
        if (finished.isEmpty())
            QVERIFY(finished.wait(10000));
        QVERIFY(!finished[0][0].toBool());
        QVERIFY(!QFile::exists(out));
        QVERIFY(QDir(d.path()).entryList({"*.part*"}, QDir::Files).isEmpty());
    }
};
QTEST_GUILESS_MAIN(MediaTests)
#include "media_tests.moc"
