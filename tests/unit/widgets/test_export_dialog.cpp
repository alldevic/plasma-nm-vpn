// SPDX-License-Identifier: MIT
// The export dialog (6.8): secrets only when asked for, the file mode, and what the format leaves out as rendered
// messages.
#include <KMessageWidget>
#include <QCheckBox>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

#include "exportdialog.h"
#include "pnv-profile.h"

static NMStringMap map(std::initializer_list<std::pair<const char *, const char *>> items)
{
    NMStringMap data;
    for (const auto &[k, v] : items)
        data.insert(QString::fromUtf8(k), QString::fromUtf8(v));
    return data;
}

static int mode(const QString &path)
{
    struct stat st;
    return stat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_mode & 0777) : -1;
}

class TestExportDialog : public QObject
{
    Q_OBJECT

    const NMStringMap data = map({{"server", "srv.stand.test"}, {"ports", "443"}, {"obfs", "salamander"},
                                  {"kill-switch", "yes"}, {"dns", "203.0.113.1"}, {"password-flags", "0"},
                                  {"obfs-password-flags", "0"}});
    const NMStringMap secrets = map({{"password", "secret-one"}, {"obfs-password", "secret-two"}});

private Q_SLOTS:
    void secretsOnlyWhenAsked()
    {
        PnvExportDialog dialog(QStringLiteral(PNV_SERVICE_HYSTERIA2), data, secrets, {});
        auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("includeSecrets"));
        QVERIFY(!box->isChecked());
        QVERIFY(!dialog.current().text.contains(QLatin1String("secret-")));
        box->setChecked(true);
        QVERIFY(dialog.current().text.contains(QLatin1String("secret-one")));
        QVERIFY(dialog.current().text.contains(QLatin1String("secret-two")));
    }

    void lostFieldsAreRendered()
    {
        PnvExportDialog dialog(QStringLiteral(PNV_SERVICE_HYSTERIA2), data, secrets, {});
        const QString text = dialog.findChild<KMessageWidget *>(QStringLiteral("exportMessage"))->text();
        QVERIFY2(text.contains(QLatin1String("kill-switch is not carried by this format")), qPrintable(text));
        QVERIFY(text.contains(QLatin1String("dns is not carried")));
        QVERIFY(!text.contains(QLatin1String("export-lost")));
    }

    void askedSecretIsNamed()
    {
        NMStringMap asked = data;
        asked.insert(QStringLiteral("password-flags"), QStringLiteral("2"));
        PnvExportDialog dialog(QStringLiteral(PNV_SERVICE_HYSTERIA2), asked, {}, {});
        dialog.findChild<QCheckBox *>(QStringLiteral("includeSecrets"))->setChecked(true);
        QVERIFY(dialog.current().warnings.contains(QStringLiteral("password is asked for every time, so it is not in the export")));
    }

    void fileModes()
    {
        QTemporaryDir dir;
        PnvExportDialog dialog(QStringLiteral(PNV_SERVICE_HYSTERIA2), data, secrets, {});
        const QString plain = dir.filePath(QStringLiteral("plain.txt")), secret = dir.filePath(QStringLiteral("secret.txt"));
        QVERIFY(dialog.saveTo(plain));
        QCOMPARE(mode(plain), 0644);
        QFile existing(secret);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.close();
        QVERIFY(existing.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther));
        dialog.findChild<QCheckBox *>(QStringLiteral("includeSecrets"))->setChecked(true);
        QVERIFY(dialog.saveTo(secret));
        QCOMPARE(mode(secret), 0600);
        QFile f(secret);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("secret-one"));
    }

    void noSymlinkFollowed()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("target")), link = dir.filePath(QStringLiteral("link"));
        QVERIFY(QFile::link(target, link));
        PnvExportDialog dialog(QStringLiteral(PNV_SERVICE_HYSTERIA2), data, secrets, {});
        QVERIFY(!dialog.saveTo(link));
        QVERIFY(!QFile::exists(target));
    }
};

QTEST_MAIN(TestExportDialog)
#include "test_export_dialog.moc"
