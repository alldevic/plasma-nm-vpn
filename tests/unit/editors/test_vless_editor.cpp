// SPDX-License-Identifier: MIT
// The VLESS XHTTP Reality editor form (6.4): setting() for a loaded profile, the choices from the value lists, the
// encryption field, the uuid secret and the prompt.
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include "passwordfield.h"
#include "pnv-profile.h"
#include "vlesswidget.h"

static const QString DataDir = QStringLiteral(PNV_SOURCE_DIR "/backends/xray/data");

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
    vpn->setServiceType(QStringLiteral(PNV_SERVICE_VLESS));
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

static QStringList listFile(const QString &name)
{
    QFile f(DataDir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QStringList entries;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (!line.trimmed().isEmpty() && !line.startsWith('#'))
            entries << QString::fromUtf8(line.trimmed());
    return entries;
}

static QStringList choices(const QComboBox *box)
{
    QStringList items;
    for (int i = 0; i < box->count(); i++)
        items << box->itemData(i).toString();
    return items;
}

class TestVlessEditor : public QObject
{
    Q_OBJECT

    const QString uuid = QStringLiteral("2f3c8e4a-91b2-4c6d-8e7f-0a1b2c3d4e5f");
    const NMStringMap full = map({{"schema", "1"},
                                  {"server", "198.51.100.1"},
                                  {"port", "443"},
                                  {"encryption", "none"},
                                  {"tls-sni", "www.stand.test"},
                                  {"reality-pbk", "Z84J2IelR9ch3k8VtlVhhs5ycBUlXA7wHBWcBrjqnAw"},
                                  {"reality-sid", "6ba85179e30d4fc2"},
                                  {"reality-spx", "/"},
                                  {"tls-fingerprint", "firefox"},
                                  {"xhttp-mode", "packet-up"},
                                  {"xhttp-path", "/x"},
                                  {"xhttp-host", "cdn.stand.test"},
                                  {"xhttp-extra", "{\"xPaddingBytes\": \"100-1000\"}"},
                                  {"kill-switch", "no"},
                                  {"allow-lan", "yes"},
                                  {"dns", "203.0.113.1"},
                                  {"extra", "{\"mux\": {\"enabled\": false}}"},
                                  {"uuid-flags", "0"}});

private Q_SLOTS:
    void roundTrip()
    {
        VlessWidget widget(profile(full, map({{"uuid", uuid.toUtf8().constData()}})), nullptr, DataDir);
        QVERIFY(widget.isValid());
        const NetworkManager::VpnSetting vpn = saved(widget);
        QCOMPARE(vpn.data(), full);
        QCOMPARE(vpn.secrets(), (NMStringMap{{QStringLiteral("uuid"), uuid}}));
        QVERIFY(vpn.persistent());
    }

    void newProfile()
    {
        VlessWidget widget(profile({}), nullptr, DataDir);
        const NMStringMap data = saved(widget).data();
        QCOMPARE(data.value(QStringLiteral("encryption")), QStringLiteral("none"));
        QCOMPARE(data.value(QStringLiteral("schema")), QStringLiteral("1"));
        // The first entries of the lists, which are the core's defaults.
        QCOMPARE(data.value(QStringLiteral("tls-fingerprint")), listFile(QStringLiteral("tls-fingerprint.list")).first());
        QCOMPARE(data.value(QStringLiteral("xhttp-mode")), listFile(QStringLiteral("xhttp-mode.list")).first());
        QVERIFY(saved(widget).persistent());
    }

    void choicesAreTheListFiles()
    {
        VlessWidget widget(profile(full), nullptr, DataDir);
        QCOMPARE(choices(widget.findChild<QComboBox *>(QStringLiteral("fingerprint"))),
                 listFile(QStringLiteral("tls-fingerprint.list")));
        QCOMPARE(choices(widget.findChild<QComboBox *>(QStringLiteral("mode"))), listFile(QStringLiteral("xhttp-mode.list")));
    }

    void listsAreReadWhenTheFormOpens()
    {
        QTemporaryDir dir;
        for (const char *name : {"tls-fingerprint.list", "xhttp-mode.list", "xhttp-extra.list", "vless-extra.list"})
            QVERIFY(QFile::copy(DataDir + QLatin1Char('/') + QLatin1String(name), dir.filePath(QLatin1String(name))));
        QFile mode(dir.filePath(QStringLiteral("xhttp-mode.list")));
        QVERIFY(mode.open(QIODevice::Append));
        mode.write("test-mode\n");
        mode.close();
        VlessWidget widget(profile(full), nullptr, dir.path());
        QVERIFY(choices(widget.findChild<QComboBox *>(QStringLiteral("mode"))).contains(QStringLiteral("test-mode")));
    }

    void missingDataIsAnError()
    {
        VlessWidget widget(profile(full), nullptr, QStringLiteral("/nonexistent"));
        QVERIFY(!widget.isValid());
        bool named = false;
        for (const QLabel *label : widget.findChildren<QLabel *>(QStringLiteral("problems")))
            named = named || label->text().contains(QLatin1String("tls-fingerprint.list"));
        QVERIFY(named);
        // Nothing guessed: only the profile's own value is there.
        QCOMPARE(choices(widget.findChild<QComboBox *>(QStringLiteral("fingerprint"))), QStringList{QStringLiteral("firefox")});
    }

    void mlkemKeptByteForByte()
    {
        QFile golden(QStringLiteral(PNV_SOURCE_DIR "/tests/golden/xray-26.11.9/mlkem.json"));
        QVERIFY(golden.open(QIODevice::ReadOnly));
        const QString key = QString::fromUtf8(golden.readAll());
        const qsizetype start = key.indexOf(QLatin1String("mlkem768x25519plus."));
        const QString value = key.mid(start, key.indexOf(QLatin1Char('"'), start) - start);
        NMStringMap data = full;
        data.insert(QStringLiteral("encryption"), value);
        VlessWidget widget(profile(data, map({{"uuid", uuid.toUtf8().constData()}})), nullptr, DataDir);
        QCOMPARE(widget.findChild<QLineEdit *>(QStringLiteral("encryption"))->text(), value);
        QVERIFY(widget.isValid());
        QCOMPARE(saved(widget).data().value(QStringLiteral("encryption")), value);
        widget.findChild<QLineEdit *>(QStringLiteral("encryption"))->setText(QStringLiteral("mlkem768x25519plus.native/0rtt"));
        QVERIFY(!widget.isValid());
    }

    // The 6.8 chain: an ML-KEM value saved, exported, imported into a new profile, unchanged.
    void mlkemSurvivesExportAndImport()
    {
        QFile golden(QStringLiteral(PNV_SOURCE_DIR "/tests/golden/xray-26.11.9/mlkem.json"));
        QVERIFY(golden.open(QIODevice::ReadOnly));
        const QString key = QString::fromUtf8(golden.readAll());
        const qsizetype start = key.indexOf(QLatin1String("mlkem768x25519plus."));
        const QString value = key.mid(start, key.indexOf(QLatin1Char('"'), start) - start);
        NMStringMap data = full;
        data.insert(QStringLiteral("encryption"), value);
        VlessWidget source(profile(data, map({{"uuid", uuid.toUtf8().constData()}})), nullptr, DataDir);
        const QString link = source.exportText(true);
        QVERIFY(link.contains(value) && link.contains(uuid));
        QVERIFY(!source.exportText(false).contains(uuid));
        VlessWidget target(profile({}), nullptr, DataDir);
        target.findChild<QPlainTextEdit *>(QStringLiteral("importText"))->setPlainText(link);
        target.findChild<QPushButton *>(QStringLiteral("importButton"))->click();
        const NetworkManager::VpnSetting vpn = saved(target);
        QCOMPARE(vpn.data().value(QStringLiteral("encryption")), value);
        QCOMPARE(vpn.secrets().value(QStringLiteral("uuid")), uuid);
        for (const char *key : {"reality-pbk", "reality-sid", "tls-fingerprint", "xhttp-mode", "xhttp-path", "xhttp-host", "xhttp-extra"})
            QCOMPARE(vpn.data().value(QLatin1String(key)), data.value(QLatin1String(key)));
    }

    void invalidFields_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("label");
        QTest::newRow("xhttp-extra key") << "xhttpExtra" << "{\"noSuchKey\": 1}" << "Additional XHTTP parameters (JSON): ";
        QTest::newRow("path in extra") << "extra" << "{\"streamSettings\": {\"sockopt\": {\"path\": \"/x\"}}}"
                                       << "Additional parameters (JSON): ";
        QTest::newRow("pbk") << "pbk" << "not+base64url" << "Public key: ";
        QTest::newRow("sid") << "sid" << "xyz" << "Short ID: ";
    }

    void invalidFields()
    {
        QFETCH(QString, field);
        QFETCH(QString, value);
        QFETCH(QString, label);
        VlessWidget widget(profile(full, map({{"uuid", uuid.toUtf8().constData()}})), nullptr, DataDir);
        if (auto *edit = widget.findChild<QLineEdit *>(field))
            edit->setText(value);
        else
            widget.findChild<QPlainTextEdit *>(field)->setPlainText(value);
        QVERIFY(!widget.isValid());
        bool readable = false;
        for (const QLabel *l : widget.findChildren<QLabel *>(QStringLiteral("problems")))
            readable = readable || l->text().startsWith(label);
        QVERIFY(readable);
    }

    void uuidIsCheckedMaskedAndSecret()
    {
        VlessWidget widget(profile(full, map({{"uuid", "not-a-uuid"}})), nullptr, DataDir);
        QVERIFY(!widget.isValid());
        auto *field = widget.findChild<PasswordField *>(QStringLiteral("uuid"));
        QCOMPARE(field->findChild<QLineEdit *>()->echoMode(), QLineEdit::Password);
        field->setText(uuid);
        QVERIFY(widget.isValid());
        const NetworkManager::VpnSetting vpn = saved(widget);
        QVERIFY(!vpn.data().contains(QStringLiteral("uuid")));
        QCOMPARE(vpn.data().value(QStringLiteral("uuid-flags")), QStringLiteral("0"));
        field->setPasswordOption(PasswordField::AlwaysAsk);
        QCOMPARE(saved(widget).data().value(QStringLiteral("uuid-flags")), QStringLiteral("2"));
        QVERIFY(saved(widget).secrets().isEmpty());
    }

    void prompt()
    {
        NMStringMap data = full;
        data.insert(QStringLiteral("uuid-flags"), QStringLiteral("2"));
        PnvAuthWidget widget(profile(data), {}, vlessPromptSecrets(), nullptr);
        const auto fields = widget.findChildren<PasswordField *>();
        QCOMPARE(fields.size(), 1);
        QCOMPARE(fields[0]->objectName(), QStringLiteral("uuid"));
    }
};

QTEST_MAIN(TestVlessEditor)
#include "test_vless_editor.moc"
