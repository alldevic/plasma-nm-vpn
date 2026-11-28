// SPDX-License-Identifier: MIT
// The guard-status parser and the kill switch banner (6.2), with a stub for plasma-nm-vpn-guard.
#include <QAction>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "guardstatus.h"
#include "killswitchbanner.h"

static const char One[] = "11111111-1111-4111-8111-111111111111";
static const char Two[] = "22222222-2222-4222-8222-222222222222";

static QByteArray group(const char *uuid, const char *state = "blocked", const char *version = "1")
{
    return QByteArray("[") + uuid + "]\nversion=" + version + "\nstate=" + state + "\nreason=gave-up\nsince=1795800000\n";
}

class TestBanner : public QObject
{
    Q_OBJECT

    QTemporaryDir dir;
    QString status, stub;

    void writeFile(const QString &path, const QByteArray &text)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }

    QStringList parsed(const QByteArray &text)
    {
        writeFile(status, text);
        QStringList uuids;
        for (const PnvGuardEntry &e : pnvReadGuardStatus(status))
            uuids << e.uuid;
        return uuids;
    }

    // The stub records its arguments, puts the next guard-status in place and exits with the given code.
    void nextRun(const QByteArray &nextStatus, int code)
    {
        writeFile(stub + QStringLiteral(".next"), nextStatus);
        writeFile(stub + QStringLiteral(".rc"), QByteArray::number(code));
    }

    QStringList stubArgs()
    {
        QFile f(stub + QStringLiteral(".args"));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    PnvKillSwitchBanner *banner(QWidget *parent, PnvKillSwitchBanner::State twoState = PnvKillSwitchBanner::State::Down)
    {
        auto names = [](const QString &uuid) {
            return uuid == QLatin1String(One) ? QStringLiteral("Office <b>VPN</b>") : QString();
        };
        auto states = [twoState](const QString &uuid) {
            return uuid == QLatin1String(Two) ? twoState : PnvKillSwitchBanner::State::Down;
        };
        return new PnvKillSwitchBanner(status, stub, names, states, parent);
    }

    static QStringList actionNames(const PnvKillSwitchBanner *b)
    {
        QStringList names;
        for (const QAction *a : b->actions())
            names << a->objectName();
        return names;
    }

    static void trigger(PnvKillSwitchBanner *b, const QString &name)
    {
        for (QAction *a : b->actions())
            if (a->objectName() == name)
                return a->trigger();
        QFAIL(qPrintable(name));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        status = dir.filePath(QStringLiteral("guard-status"));
        stub = dir.filePath(QStringLiteral("guard"));
        writeFile(stub, QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"%1.args\"\ncp \"%1.next\" \"%2\"\nexit $(cat \"%1.rc\")\n")
                            .arg(stub, status)
                            .toUtf8());
        QFile(stub).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    void parser()
    {
        QFile::remove(status);
        QCOMPARE(pnvReadGuardStatus(status).size(), 0);
        QCOMPARE(parsed(""), QStringList());
        QCOMPARE(parsed(group(One)), QStringList{QLatin1String(One)});
        QCOMPARE(parsed(group(One) + "\n# a comment\n" + group(Two)), (QStringList{QLatin1String(One), QLatin1String(Two)}));
        const auto e = pnvReadGuardStatus(status);
        QCOMPARE(e[0].reason, QStringLiteral("gave-up"));
        QCOMPARE(e[0].since, 1795800000);
        // Malformed lines are skipped, the rest still counts.
        QCOMPARE(parsed("garbage\n[" + QByteArray(One) + "\nversion=1\n" + group(Two) + "=x\nno equals sign\n"),
                 QStringList{QLatin1String(Two)});
        QCOMPARE(parsed("[not-a-uuid]\nversion=1\nstate=blocked\n" + group(One)), QStringList{QLatin1String(One)});
        // Another version, another state, a missing version.
        QCOMPARE(parsed(group(One, "blocked", "2") + group(Two)), QStringList{QLatin1String(Two)});
        QCOMPARE(parsed(group(One, "handing-over") + group(Two)), QStringList{QLatin1String(Two)});
        QCOMPARE(parsed("[" + QByteArray(One) + "]\nstate=blocked\n"), QStringList());
        QCOMPARE(parsed(QByteArray(4096, '\xff')), QStringList());
    }

    void visibility_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<bool>("visible");
        QTest::newRow("missing") << QByteArray() << false;
        QTest::newRow("empty") << QByteArray("") << false;
        QTest::newRow("one") << group(One) << true;
        QTest::newRow("several") << group(One) + group(Two) << true;
        QTest::newRow("malformed") << QByteArray("[x\nversion=\n=\n") << false;
        QTest::newRow("unknown version") << group(One, "blocked", "7") << false;
        QTest::newRow("unknown state") << group(One, "handing-over") << false;
    }

    void visibility()
    {
        QFETCH(QByteArray, text);
        QFETCH(bool, visible);
        if (text.isNull())
            QFile::remove(status);
        else
            writeFile(status, text);
        QWidget form;
        QCOMPARE(banner(&form)->isVisibleTo(&form), visible);
    }

    void listsNamesOrUuids()
    {
        writeFile(status, group(One) + group(Two));
        QWidget form;
        auto *b = banner(&form, PnvKillSwitchBanner::State::Connecting);
        QVERIFY(b->text().contains(QLatin1String("Kill switch block active")));
        // The name is escaped; a UUID without a profile is shown as itself; the state of a live connection.
        QVERIFY(b->text().contains(QLatin1String("Office &lt;b&gt;VPN&lt;/b&gt;")));
        QVERIFY(b->text().contains(QLatin1String(Two) + QLatin1String(": the connection could not be restored")));
        QVERIFY(b->text().contains(QLatin1String("(connecting)")));
        QCOMPARE(actionNames(b), (QStringList{QStringLiteral("clear-") + QLatin1String(One),
                                              QStringLiteral("clear-") + QLatin1String(Two), QStringLiteral("clear-all")}));
        writeFile(status, group(One));
        b->reload();
        QCOMPARE(actionNames(b), QStringList{QStringLiteral("clear-") + QLatin1String(One)});
    }

    void clearOne()
    {
        writeFile(status, group(One) + group(Two));
        QWidget form;
        auto *b = banner(&form);
        QSignalSpy done(b, &PnvKillSwitchBanner::finished);
        nextRun(group(Two), 0);
        trigger(b, QStringLiteral("clear-") + QLatin1String(One));
        QVERIFY(done.wait());
        QCOMPARE(done[0][0].toBool(), true);
        QCOMPARE(stubArgs(), (QStringList{QStringLiteral("--clear"), QLatin1String(One)}));
        QCOMPARE(b->entries().size(), 1);
        nextRun("", 0);
        trigger(b, QStringLiteral("clear-") + QLatin1String(Two));
        QVERIFY(done.wait());
        QVERIFY(!b->isVisibleTo(&form));
    }

    void clearAllKeepsTheLiveOne()
    {
        writeFile(status, group(One) + group(Two));
        QWidget form;
        auto *b = banner(&form, PnvKillSwitchBanner::State::Connecting);
        QSignalSpy done(b, &PnvKillSwitchBanner::finished);
        nextRun(group(Two), 0);
        trigger(b, QStringLiteral("clear-all"));
        QVERIFY(done.wait());
        QCOMPARE(stubArgs(), QStringList{QStringLiteral("--clear")});
        QCOMPARE(b->entries().size(), 1);
        QCOMPARE(b->entries()[0].uuid, QLatin1String(Two));
        QVERIFY(b->isVisibleTo(&form) && b->text().contains(QLatin1String("(connecting)")));
    }

    void refusedChangesNothing()
    {
        writeFile(status, group(One));
        QWidget form;
        auto *b = banner(&form);
        QSignalSpy done(b, &PnvKillSwitchBanner::finished);
        nextRun(group(One), 126);
        b->clear(QLatin1String(One));
        QVERIFY(done.wait());
        QCOMPARE(done[0][0].toBool(), false);
        QVERIFY(b->isVisibleTo(&form) && b->entries().size() == 1);
        QVERIFY(b->text().contains(QLatin1String("was not cleared")));
    }

    void missingTool()
    {
        writeFile(status, group(One));
        QWidget form;
        auto *b = new PnvKillSwitchBanner(status, dir.filePath(QStringLiteral("absent")), [](const QString &) { return QString(); },
                                          [](const QString &) { return PnvKillSwitchBanner::State::Down; }, &form);
        QSignalSpy done(b, &PnvKillSwitchBanner::finished);
        b->clear();
        QVERIFY(done.wait());
        QCOMPARE(done[0][0].toBool(), false);
        QVERIFY(b->isVisibleTo(&form));
    }
};

QTEST_MAIN(TestBanner)
#include "test_banner.moc"
