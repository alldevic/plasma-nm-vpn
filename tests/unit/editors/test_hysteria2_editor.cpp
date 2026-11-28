// SPDX-License-Identifier: MIT
// The Hysteria2 editor form (6.3): what setting() returns to plasma-nm for a loaded profile, the secrets, the
// flags, persistent and schema; the prompt widget.
#include <QLabel>
#include <QLineEdit>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include "hysteria2widget.h"
#include "passwordfield.h"
#include "pnv-profile.h"
#include "tlsblock.h"

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
    vpn->setServiceType(QStringLiteral(PNV_SERVICE_HYSTERIA2));
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

class TestHysteria2Editor : public QObject
{
    Q_OBJECT

    const QString extraList = QStringLiteral(PNV_SOURCE_DIR "/backends/sb/data/hysteria2-extra.list");

    // The full data of a profile as the form writes it.
    const NMStringMap full = map({{"schema", "1"},
                                  {"server", "srv.stand.test"},
                                  {"ports", "443,20000-20100"},
                                  {"tls-sni", "srv.stand.test"},
                                  {"tls-alpn", "h3"},
                                  {"tls-insecure", "no"},
                                  {"obfs", "salamander"},
                                  {"hop-interval", "30"},
                                  {"up-mbps", "50"},
                                  {"down-mbps", "200"},
                                  {"kill-switch", "yes"},
                                  {"allow-lan", "yes"},
                                  {"dns", "203.0.113.1"},
                                  {"mtu", "1400"},
                                  {"extra", "{\"udp_fragment\": true}"},
                                  {"password-flags", "1"},
                                  {"obfs-password-flags", "0"}});

private Q_SLOTS:
    void roundTrip()
    {
        Hysteria2Widget widget(profile(full, map({{"password", "secret one"}, {"obfs-password", "secret two"}})), nullptr,
                               extraList);
        QVERIFY2(widget.isValid(), "the full profile is valid");
        const NetworkManager::VpnSetting vpn = saved(widget);
        QCOMPARE(vpn.serviceType(), QStringLiteral(PNV_SERVICE_HYSTERIA2));
        QCOMPARE(vpn.data(), full);
        QCOMPARE(vpn.secrets(), map({{"password", "secret one"}, {"obfs-password", "secret two"}}));
        QVERIFY(vpn.persistent());
        QVERIFY(!vpn.data().contains(QStringLiteral("port")) && !vpn.data().contains(QStringLiteral("password")));
    }

    void newProfileIsPersistentWithSchema()
    {
        Hysteria2Widget widget(profile({}), nullptr, extraList);
        const NetworkManager::VpnSetting vpn = saved(widget);
        QVERIFY(vpn.persistent());
        QCOMPARE(vpn.data().value(QStringLiteral("schema")), QStringLiteral("1"));
        QVERIFY(!widget.isValid()); // no server, no ports
    }

    void secretOptions()
    {
        NMStringMap data = full;
        data.insert(QStringLiteral("password-flags"), QStringLiteral("2")); // asked every time
        data.remove(QStringLiteral("obfs"));
        Hysteria2Widget widget(profile(data, map({{"password", "stale"}, {"obfs-password", "unused"}})), nullptr, extraList);
        const NetworkManager::VpnSetting vpn = saved(widget);
        QCOMPARE(vpn.data().value(QStringLiteral("password-flags")), QStringLiteral("2"));
        // Without obfuscation its password is not required, whatever the field says.
        QCOMPARE(vpn.data().value(QStringLiteral("obfs-password-flags")), QStringLiteral("4"));
        QVERIFY(vpn.secrets().isEmpty());
        QCOMPARE(widget.findChild<PasswordField *>(QStringLiteral("password"))->passwordOption(), PasswordField::AlwaysAsk);
    }

    void secretsAreMasked()
    {
        Hysteria2Widget widget(profile(full, map({{"password", "x"}})), nullptr, extraList);
        for (const char *name : {"password", "obfs-password"}) {
            auto *field = widget.findChild<PasswordField *>(QLatin1String(name));
            QVERIFY(field);
            QCOMPARE(field->findChild<QLineEdit *>()->echoMode(), QLineEdit::Password);
        }
    }

    void invalidFieldsHaveReadableMessages()
    {
        Hysteria2Widget widget(profile(full, map({{"password", "x"}})), nullptr, extraList);
        auto *tls = widget.findChild<PnvTlsBlock *>();
        tls->findChild<QLineEdit *>(QStringLiteral("sni"))->setText(QStringLiteral("srv.stand.test\nx"));
        QVERIFY(!widget.isValid());
        QVERIFY(tls->findChild<QLabel *>(QStringLiteral("problems"))->text().startsWith(QLatin1String("Server name (SNI): ")));
        tls->findChild<QLineEdit *>(QStringLiteral("sni"))->setText(QStringLiteral("srv.stand.test"));
        QVERIFY(widget.isValid());
        auto *extra = widget.findChild<QPlainTextEdit *>(QStringLiteral("extra"));
        extra->setPlainText(QStringLiteral("{\"detour\": \"x\"}"));
        QVERIFY(!widget.isValid());
        extra->setPlainText(QStringLiteral("{\"no_such_key\": 1}"));
        QVERIFY(!widget.isValid());
    }

    void hintIsShown()
    {
        Hysteria2Widget widget(profile(full), nullptr, extraList);
        auto *hint = widget.findChild<QLabel *>(QStringLiteral("killSwitchHint"));
        QVERIFY(hint && hint->isVisibleTo(&widget));
        QCOMPARE(hint->text(), QStringLiteral("The kill switch applies only when all traffic goes through the VPN"));
    }

    void importFillsTheForm()
    {
        Hysteria2Widget widget(profile({}), nullptr, extraList);
        QFile link(QStringLiteral(PNV_SOURCE_DIR "/tests/unit/data/import/bench-hysteria2.txt"));
        QVERIFY(link.open(QIODevice::ReadOnly));
        auto *paste = widget.findChild<QPlainTextEdit *>(QStringLiteral("importText"));
        paste->setPlainText(QString::fromUtf8(link.readAll()));
        QSignalSpy valid(&widget, &SettingWidget::validChanged);
        widget.findChild<QPushButton *>(QStringLiteral("importButton"))->click();
        QVERIFY(paste->toPlainText().isEmpty());
        QVERIFY(widget.isValid());
        QVERIFY(!valid.isEmpty() && valid.last().at(0).toBool());
        const NetworkManager::VpnSetting vpn = saved(widget);
        QCOMPARE(vpn.data().value(QStringLiteral("server")), QStringLiteral("srv.stand.test"));
        QCOMPARE(vpn.data().value(QStringLiteral("obfs")), QStringLiteral("salamander"));
        QCOMPARE(vpn.secrets().value(QStringLiteral("obfs-password")), QStringLiteral("79b63d51a9800c5338f711cff5890afa"));
    }

    void importAsksBeforeReplacing()
    {
        Hysteria2Widget widget(profile(full, map({{"password", "mine"}})), nullptr, extraList);
        int asked = 0;
        widget.setImportConfirmation([&asked] {
            asked++;
            return false;
        });
        const NMStringMap before = saved(widget).data();
        QVERIFY(!widget.applyImport(map({{"server", "other.example"}, {"ports", "443"}}), {}));
        QCOMPARE(asked, 1);
        QCOMPARE(saved(widget).data(), before);
        widget.setImportConfirmation([] {
            return true;
        });
        QVERIFY(widget.applyImport(map({{"server", "other.example"}, {"ports", "443"}}), {}));
        QCOMPARE(saved(widget).data().value(QStringLiteral("server")), QStringLiteral("other.example"));
        // The optional secret the import has none of is not required.
        QCOMPARE(saved(widget).data().value(QStringLiteral("obfs-password-flags")), QStringLiteral("4"));
    }

    void prompt()
    {
        NMStringMap data = full;
        data.insert(QStringLiteral("password-flags"), QStringLiteral("2"));
        data.insert(QStringLiteral("obfs-password-flags"), QStringLiteral("1"));
        PnvAuthWidget widget(profile(data, map({{"obfs-password", "stored"}})), {}, hysteria2PromptSecrets(), nullptr);
        const auto fields = widget.findChildren<PasswordField *>();
        QCOMPARE(fields.size(), 1);
        QCOMPARE(fields[0]->objectName(), QStringLiteral("password"));
        QVERIFY(!widget.isValid());
        fields[0]->setText(QStringLiteral("typed"));
        QVERIFY(widget.isValid());
        NetworkManager::VpnSetting vpn;
        vpn.fromMap(widget.setting());
        QCOMPARE(vpn.secrets(), map({{"password", "typed"}}));
        // NM's hints name the secret it wants, e.g. after a wrong one.
        PnvAuthWidget hinted(profile(data, map({{"obfs-password", "stored"}})), {QStringLiteral("obfs-password")}, hysteria2PromptSecrets(), nullptr);
        QCOMPARE(hinted.findChildren<PasswordField *>().size(), 2);
    }
};

QTEST_MAIN(TestHysteria2Editor)
#include "test_hysteria2_editor.moc"
