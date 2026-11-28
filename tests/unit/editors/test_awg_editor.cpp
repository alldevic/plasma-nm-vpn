// SPDX-License-Identifier: MIT
// The AmneziaWG editor form (6.5): setting() for a loaded profile, the parameter table driven by the allowlist,
// the single address, allowed-ips and the kill switch, the three secrets and the prompt.
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "awgwidget.h"
#include "passwordfield.h"
#include "pnv-profile.h"

static const QString DataDir = QStringLiteral(PNV_SOURCE_DIR "/backends/awg/data");
static const char KeyA[] = "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWY=";
static const char KeyB[] = "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDE=";

static NMStringMap map(std::initializer_list<std::pair<const char *, const char *>> items)
{
    NMStringMap data;
    for (const auto &[k, v] : items)
        data.insert(QString::fromUtf8(k), QString::fromUtf8(v));
    return data;
}

static NetworkManager::VpnSetting::Ptr profile(const NMStringMap &data, const NMStringMap &secrets = {})
{
    NetworkManager::VpnSetting::Ptr vpn(new NetworkManager::VpnSetting);
    vpn->setServiceType(QStringLiteral(PNV_SERVICE_AMNEZIAWG));
    vpn->setData(data);
    vpn->setSecrets(secrets);
    return vpn;
}

static NetworkManager::VpnSetting saved(const SettingWidget &widget)
{
    NetworkManager::VpnSetting vpn;
    vpn.fromMap(widget.setting());
    return vpn;
}

static QString problems(const QWidget &widget)
{
    QStringList all;
    for (const QLabel *l : widget.findChildren<QLabel *>(QStringLiteral("problems")))
        if (!l->text().isEmpty())
            all << l->text();
    return all.join(QLatin1Char('\n'));
}

class TestAwgEditor : public QObject
{
    Q_OBJECT

    const NMStringMap full = map({{"schema", "1"},
                                  {"server", "198.51.100.1"},
                                  {"port", "51820"},
                                  {"address", "100.64.0.2/24"},
                                  {"peer-public-key", KeyA},
                                  {"allowed-ips", "0.0.0.0/0"},
                                  {"keepalive", "25"},
                                  {"awg-jc", "4"},
                                  {"awg-h1", "1100000000-1199999999"},
                                  {"awg-i1", "<b 0xc2000000010840><r 32>"},
                                  {"kill-switch", "yes"},
                                  {"allow-lan", "yes"},
                                  {"dns", "203.0.113.1"},
                                  {"mtu", "1420"},
                                  {"private-key-flags", "0"},
                                  {"preshared-key-flags", "4"},
                                  {"awg-headerprotectionkey-flags", "0"}});
    const NMStringMap secrets = map({{"private-key", KeyB}, {"awg-headerprotectionkey", KeyA}});

    QComboBox *keyBox(QWidget &widget, int n)
    {
        return widget.findChild<QComboBox *>(QStringLiteral("paramKey%1").arg(n));
    }

private Q_SLOTS:
    void roundTrip()
    {
        AwgWidget widget(profile(full, secrets), nullptr, DataDir);
        QVERIFY2(widget.isValid(), qPrintable(problems(widget)));
        const NetworkManager::VpnSetting vpn = saved(widget);
        QCOMPARE(vpn.data(), full);
        QCOMPARE(vpn.secrets(), secrets);
        QVERIFY(vpn.persistent());
    }

    void newProfile()
    {
        AwgWidget widget(profile({}), nullptr, DataDir);
        const NetworkManager::VpnSetting vpn = saved(widget);
        QCOMPARE(vpn.data().value(QStringLiteral("schema")), QStringLiteral("1"));
        QVERIFY(vpn.persistent());
        // The optional keys, left empty, are not required.
        QCOMPARE(vpn.data().value(QStringLiteral("preshared-key-flags")), QStringLiteral("4"));
        QCOMPARE(vpn.data().value(QStringLiteral("awg-headerprotectionkey-flags")), QStringLiteral("4"));
    }

    void keysComeFromTheAllowlist()
    {
        AwgWidget widget(profile(full, secrets), nullptr, DataDir);
        QComboBox *box = keyBox(widget, 0);
        QVERIFY(box);
        QStringList keys;
        for (int i = 0; i < box->count(); i++)
            keys << box->itemData(i).toString();
        QFile list(DataDir + QStringLiteral("/amneziawg-params.list"));
        QVERIFY(list.open(QIODevice::ReadOnly));
        QStringList expected;
        for (const QByteArray &line : list.readAll().split('\n'))
            if (!line.trimmed().isEmpty() && !line.startsWith('#'))
                expected << QString::fromUtf8(line.split(' ').first());
        QCOMPARE(keys, expected);
        QVERIFY(!box->isEditable());
        // The names are AmneziaWG's own.
        QCOMPARE(box->itemText(0), QStringLiteral("Jc"));
    }

    void addedKeyAppearsWithoutRebuild()
    {
        QTemporaryDir dir;
        QVERIFY(QFile::copy(DataDir + QStringLiteral("/amneziawg-params.list"), dir.filePath(QStringLiteral("amneziawg-params.list"))));
        QFile list(dir.filePath(QStringLiteral("amneziawg-params.list")));
        QVERIFY(list.open(QIODevice::Append));
        list.write("awg-pnvtest PnvTest uint16\n");
        list.close();
        AwgWidget widget(profile(full, secrets), nullptr, dir.path());
        QVERIFY(keyBox(widget, 0)->findData(QStringLiteral("awg-pnvtest")) >= 0);
    }

    void noDuplicates()
    {
        AwgWidget widget(profile(full, secrets), nullptr, DataDir);
        QComboBox *first = keyBox(widget, 0);
        const QString used = first->currentData().toString();
        auto *add = widget.findChild<QPushButton *>(QStringLiteral("addParam"));
        add->click();
        QComboBox *added = keyBox(widget, 3);
        QVERIFY(added);
        QVERIFY(added->currentData().toString() != used);
        auto *model = qobject_cast<QStandardItemModel *>(added->model());
        QVERIFY(!model->item(added->findData(used))->isEnabled());
    }

    void wrongValues_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value");
        QTest::newRow("uint16 out of range") << "awg-jc" << "70000";
        QTest::newRow("range reversed") << "awg-h1" << "9-1";
        QTest::newRow("onoff") << "awg-randomtrailers" << "yes";
        QTest::newRow("chain with #") << "awg-i1" << "<b 0x01>#";
    }

    void wrongValues()
    {
        QFETCH(QString, key);
        QFETCH(QString, value);
        NMStringMap data = full;
        data.insert(key, value);
        AwgWidget widget(profile(data, secrets), nullptr, DataDir);
        QVERIFY(!widget.isValid());
        const QString text = problems(widget);
        QVERIFY2(!text.contains(QLatin1String("bad-")) && !text.contains(QLatin1String("out-of-range")), qPrintable(text));
        QVERIFY2(text.contains(QLatin1Char(':')), qPrintable(text));
    }

    void oneIPv4Address_data()
    {
        QTest::addColumn<QString>("address");
        QTest::newRow("two") << "100.64.0.2/24, 100.64.0.3/24";
        QTest::newRow("IPv6") << "fd00::2/64";
    }

    void oneIPv4Address()
    {
        QFETCH(QString, address);
        AwgWidget widget(profile(full, secrets), nullptr, DataDir);
        widget.findChild<QLineEdit *>(QStringLiteral("address"))->setText(address);
        QVERIFY(!widget.isValid());
        QVERIFY(problems(widget).contains(QLatin1String("Address: ")));
    }

    void killSwitchFollowsAllowedIps()
    {
        AwgWidget widget(profile(full, secrets), nullptr, DataDir);
        auto *allowed = widget.findChild<QLineEdit *>(QStringLiteral("allowedIps"));
        auto *box = widget.findChild<QCheckBox *>(QStringLiteral("killSwitch"));
        auto *reason = widget.findChild<QLabel *>(QStringLiteral("killSwitchReason"));
        auto *hint = widget.findChild<QLabel *>(QStringLiteral("killSwitchHint"));
        QVERIFY(box->isEnabled());
        allowed->setText(QStringLiteral("10.0.0.0/8"));
        QVERIFY(!box->isEnabled() && reason->isVisibleTo(&widget) && !reason->text().isEmpty());
        QCOMPARE(saved(widget).data().value(QStringLiteral("kill-switch")), QStringLiteral("yes")); // kept
        allowed->setText(QStringLiteral("0.0.0.0/1, 128.0.0.0/1"));
        QVERIFY(box->isEnabled() && !reason->isVisibleTo(&widget));
        QCOMPARE(saved(widget).data().value(QStringLiteral("allowed-ips")), QStringLiteral("0.0.0.0/0"));
        QVERIFY(hint->isVisibleTo(&widget));
        allowed->setText(QStringLiteral("0.0.0.0/0, ::/0"));
        QVERIFY(!widget.isValid());
    }

    void secretsAreMaskedAndSecret()
    {
        AwgWidget widget(profile(full, secrets), nullptr, DataDir);
        for (const char *name : {"private-key", "preshared-key", "awg-headerprotectionkey"}) {
            auto *field = widget.findChild<PasswordField *>(QLatin1String(name));
            QVERIFY(field);
            QCOMPARE(field->findChild<QLineEdit *>()->echoMode(), QLineEdit::Password);
        }
        widget.findChild<PasswordField *>(QStringLiteral("preshared-key"))->setPasswordOption(PasswordField::StoreForAllUsers);
        widget.findChild<PasswordField *>(QStringLiteral("preshared-key"))->setText(QStringLiteral("short"));
        QVERIFY(!widget.isValid());
        widget.findChild<PasswordField *>(QStringLiteral("preshared-key"))->setText(QLatin1String(KeyA));
        QVERIFY(widget.isValid());
        const NetworkManager::VpnSetting vpn = saved(widget);
        for (const char *name : {"private-key", "preshared-key", "awg-headerprotectionkey"})
            QVERIFY(vpn.secrets().contains(QLatin1String(name)) && !vpn.data().contains(QLatin1String(name)));
    }

    void prompt()
    {
        NMStringMap data = full;
        data.insert(QStringLiteral("private-key-flags"), QStringLiteral("2"));
        PnvAuthWidget widget(profile(data, map({{"awg-headerprotectionkey", KeyA}})), {}, awgPromptSecrets(), nullptr);
        const auto fields = widget.findChildren<PasswordField *>();
        // preshared-key is not required, the header protection key is stored.
        QCOMPARE(fields.size(), 1);
        QCOMPARE(fields[0]->objectName(), QStringLiteral("private-key"));
    }
};

QTEST_MAIN(TestAwgEditor)
#include "test_awg_editor.moc"
