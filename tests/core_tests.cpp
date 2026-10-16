// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/project.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace ff;
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void edits() {
        Project p;
        p.media.append({"m", "/missing.mov", 4000000, 320, 240, true, true});
        p.clips.append({"a", "m", 0, 0, 0, 100});
        auto right = p.split("a", 40);
        QCOMPARE(p.clips.size(), 2);
        QCOMPARE(p.clip(right).in, 40);
        QCOMPARE(p.clip("a").duration, 40);
        QVERIFY_EXCEPTION_THROWN(p.move(right, 0, 30), Error);
        p.move(right, 1, 50);
        p.trim(right, 45, 30);
        QCOMPARE(p.clip(right).in, 45);
        QCOMPARE(p.clip(right).duration, 30);
        p.tracks[1].locked = true;
        QVERIFY_EXCEPTION_THROWN(p.erase(right), Error);
        p.tracks[1].locked = false;
        p.erase(right);
        QCOMPARE(p.clips.size(), 1);
        QVERIFY_EXCEPTION_THROWN(p.trim("a", 99, 5), Error);
    }
    void roundTrip() {
        QTemporaryDir dir;
        Project p;
        p.rate = {30000, 1001};
        p.media.append({"m", dir.filePath("a.mov"), 2000000, 320, 240, true, false});
        p.clips.append({"c", "m", 0, 0, 0, 30});
        p.save(dir.filePath("p.ffg"));
        auto q = Project::load(dir.filePath("p.ffg"));
        QCOMPARE(q.rate.num, 30000);
        QCOMPARE(q.media[0].path, p.media[0].path);
        QCOMPARE(q.clips[0].duration, 30);
        QVERIFY(!QFile::exists(q.media[0].path));
    }
    void rejectsInvalid() {
        Project p;
        p.media.append({"m", "/x", 1000000, 1, 1, true, false});
        p.clips.append({"a", "m", 0, 0, 0, 26});
        QVERIFY_EXCEPTION_THROWN(p.validate(), Error);
        QTemporaryDir d;
        QFile f(d.filePath("bad.ffg"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"version\":99}");
        f.close();
        QVERIFY_EXCEPTION_THROWN(Project::load(f.fileName()), Error);
    }
    void protectsSources() {
        QTemporaryDir d;
        QString path = d.filePath("footage.mov");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("original footage");
        f.close();
        Project p;
        p.media.append({"m", path, 1000000, 16, 16, true, false});
        QVERIFY_EXCEPTION_THROWN(p.save(path), Error);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("original footage"));
    }
    void largeRational() {
        Rate r{240000, 10000};
        QCOMPARE(r.us(1000000000), qint64(41666666666666));
    }
    void rational() {
        Rate r{30000, 1001};
        QCOMPARE(r.us(30000), qint64(1001000000));
        QCOMPARE(r.frames(1001000000), qint64(30000));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
