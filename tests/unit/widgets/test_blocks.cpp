// SPDX-License-Identifier: MIT
// The shared form blocks (6.1): load → write, the 2.2 validators behind isValid(), the parameters, and a message
// for every validator code.
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTest>

#include "advancedblock.h"
#include "authblock.h"
#include "messages.h"
#include "protectionblock.h"
#include "serverblock.h"
#include "tlsblock.h"

static const QString Hy2 = QStringLiteral(PNV_SERVICE_HYSTERIA2);
static const QString Vless = QStringLiteral(PNV_SERVICE_VLESS);

static NMStringMap written(const PnvBlock &block)
{
    NMStringMap data;
    block.write(data);
    return data;
}

static NMStringMap map(std::initializer_list<std::pair<const char *, const char *>> items)
{
    NMStringMap data;
    for (const auto &[k, v] : items)
        data.insert(QString::fromUtf8(k), QString::fromUtf8(v));
    return data;
}

class TestBlocks : public QObject
{
    Q_OBJECT

    QTemporaryDir dir;
    QString extraList, fingerprintList;

    QString writeList(const char *name, const char *text)
    {
        QFile f(dir.filePath(QString::fromLatin1(name)));
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot write %s", name);
        f.write(text);
        return f.fileName();
    }

    static void typeInto(PnvBlock &block, const char *name, const QString &text)
    {
        block.findChild<QLineEdit *>(QString::fromLatin1(name))->setText(text);
    }

    static void expectProblem(const PnvBlock &block, const char *label)
    {
        const QStringList problems = block.problems();
        QVERIFY(!block.isValid());
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems[0].startsWith(QString::fromUtf8(label) + QLatin1Char(':')), qPrintable(problems[0]));
        // Rendered, not the code name.
        QVERIFY2(!problems[0].contains(QLatin1String("bad-")) && !problems[0].contains(QLatin1String("control-char")),
                 qPrintable(problems[0]));
        QCOMPARE(block.findChild<QLabel *>(QStringLiteral("problems"))->text(), problems[0]);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        extraList = writeList("extra.list", "# test allowlist\ntls\ntls.fragment\nconnect_timeout\n");
        fingerprintList = writeList("tls-fingerprint.list", "chrome\nfirefox\n");
    }

    void serverRoundTrip()
    {
        const NMStringMap data = map({{"server", "vpn.example.com"}, {"port", "443"}});
        PnvServerBlock block(Vless, PnvServerBlock::SinglePort);
        block.load(data);
        QCOMPARE(written(block), data);
        QVERIFY(block.isValid());
    }

    void serverPortList()
    {
        const NMStringMap data = map({{"server", "vpn.example.com"}, {"ports", "443,5000-6000"}});
        PnvServerBlock block(Hy2, PnvServerBlock::PortList);
        block.load(data);
        QCOMPARE(written(block), data);
        QVERIFY(!written(block).contains(QStringLiteral("port")));
        QVERIFY(block.isValid());
    }

    void serverInvalid()
    {
        PnvServerBlock single(Vless, PnvServerBlock::SinglePort);
        single.load(map({{"server", "vpn.example.com"}, {"port", "70000"}}));
        expectProblem(single, "Port");
        typeInto(single, "port", QStringLiteral("443"));
        typeInto(single, "server", QStringLiteral("vpn.example.com\nevil"));
        expectProblem(single, "Server");
        typeInto(single, "server", QString());
        expectProblem(single, "Server");

        PnvServerBlock list(Hy2, PnvServerBlock::PortList);
        list.load(map({{"server", "vpn.example.com"}, {"ports", "443,abc"}}));
        expectProblem(list, "Ports");
    }

    void tlsHysteria2()
    {
        const NMStringMap data = map({{"tls-sni", "cdn.example.com"}, {"tls-alpn", "h3"}, {"tls-insecure", "no"}});
        PnvTlsBlock block(Hy2);
        block.load(data);
        QCOMPARE(written(block), data);
        QVERIFY(block.isValid());
        QVERIFY(!block.findChild<QComboBox *>(QStringLiteral("fingerprint"))->isVisibleTo(&block));
    }

    void tlsWithoutPin()
    {
        PnvTlsBlock block(Hy2);
        NMStringMap data = map({{"tls-sni", "cdn.example.com"}, {"tls-insecure", "no"}});
        data.insert(QStringLiteral("tls-pin-sha256"), QString(64, QLatin1Char('a')));
        block.load(data);
        QVERIFY(!block.findChild<QLineEdit *>(QStringLiteral("pin"))->isVisibleTo(&block));
        QVERIFY(!written(block).contains(QStringLiteral("tls-pin-sha256")));

        PnvTlsBlock withPin(Hy2, {}, true);
        withPin.load(data);
        QVERIFY(withPin.findChild<QLineEdit *>(QStringLiteral("pin"))->isVisibleTo(&withPin));
        QCOMPARE(written(withPin), data);
    }

    void tlsFingerprintFromList()
    {
        PnvTlsBlock block(Vless, fingerprintList);
        auto *box = block.findChild<QComboBox *>(QStringLiteral("fingerprint"));
        QVERIFY(box->isVisibleTo(&block));
        QCOMPARE(box->count(), 3); // Default, then the list in file order
        QCOMPARE(box->itemData(1).toString(), QStringLiteral("chrome"));
        const NMStringMap data = map({{"tls-sni", "a.example.com"}, {"tls-insecure", "no"}, {"tls-fingerprint", "firefox"}});
        block.load(data);
        QCOMPARE(written(block), data);
        // A value outside the list is kept for the validator to name.
        block.load(map({{"tls-sni", "a.example.com"}, {"tls-insecure", "no"}, {"tls-fingerprint", "edge"}}));
        QCOMPARE(written(block).value(QStringLiteral("tls-fingerprint")), QStringLiteral("edge"));
    }

    void protectionRoundTrip()
    {
        const NMStringMap data = map({{"kill-switch", "yes"}, {"allow-lan", "no"}, {"dns", "1.1.1.1, 9.9.9.9"}});
        PnvProtectionBlock block(Hy2);
        block.load(data);
        QCOMPARE(written(block), data);
        QVERIFY(block.isValid());
        PnvProtectionBlock defaults(Hy2);
        defaults.load({});
        QCOMPARE(written(defaults), map({{"kill-switch", "no"}, {"allow-lan", "yes"}}));
        typeInto(defaults, "dns", QStringLiteral("1.1.1.1, example.com"));
        expectProblem(defaults, "DNS servers");
    }

    void protectionKillSwitchDisabled()
    {
        const NMStringMap data = map({{"kill-switch", "yes"}, {"allow-lan", "yes"}});
        PnvProtectionBlock block(Hy2);
        auto *hint = block.findChild<QLabel *>(QStringLiteral("killSwitchHint"));
        auto *reason = block.findChild<QLabel *>(QStringLiteral("killSwitchReason"));
        auto *box = block.findChild<QCheckBox *>(QStringLiteral("killSwitch"));
        QVERIFY(hint->isVisibleTo(&block) && !hint->text().isEmpty());
        QVERIFY(!reason->isVisibleTo(&block));
        block.load(data);
        block.setKillSwitchDisabled(QStringLiteral("Allowed IPs do not include 0.0.0.0/0"));
        QVERIFY(!box->isEnabled());
        QVERIFY(reason->isVisibleTo(&block));
        QCOMPARE(reason->text(), QStringLiteral("Allowed IPs do not include 0.0.0.0/0"));
        QVERIFY(hint->isVisibleTo(&block));
        QCOMPARE(written(block), data);
        block.load(data);
        QCOMPARE(written(block), data);
        block.setKillSwitchDisabled({});
        QVERIFY(box->isEnabled() && !reason->isVisibleTo(&block));
    }

    void advancedRoundTrip()
    {
        const NMStringMap data = map({{"mtu", "1400"}, {"extra", "{\"tls\": {\"fragment\": true}}"}});
        PnvAdvancedBlock block(Hy2, extraList);
        block.load(data);
        QCOMPARE(written(block), data);
        QVERIFY2(block.isValid(), qPrintable(block.problems().join(QLatin1Char('\n'))));
    }

    void advancedWithoutExtra()
    {
        PnvAdvancedBlock block(QStringLiteral(PNV_SERVICE_AMNEZIAWG));
        QVERIFY(!block.findChild<QPlainTextEdit *>(QStringLiteral("extra"))->isVisibleTo(&block));
        block.load(map({{"mtu", "1420"}, {"extra", "{}"}}));
        QCOMPARE(written(block), map({{"mtu", "1420"}}));
        typeInto(block, "mtu", QStringLiteral("100"));
        expectProblem(block, "MTU");
    }

    void advancedInvalidExtra()
    {
        PnvAdvancedBlock block(Hy2, extraList);
        auto *extra = block.findChild<QPlainTextEdit *>(QStringLiteral("extra"));
        extra->setPlainText(QStringLiteral("{\"tls\": "));
        expectProblem(block, "Additional parameters (JSON)");
        extra->setPlainText(QStringLiteral("{\"udp_fragment\": true}"));
        expectProblem(block, "Additional parameters (JSON)");
        QVERIFY(block.problems()[0].contains(QLatin1String("udp_fragment")));
    }

    void missingListFile()
    {
        PnvAdvancedBlock block(Hy2, dir.filePath(QStringLiteral("absent.list")));
        QVERIFY(!block.isValid());
        QVERIFY(block.problems()[0].contains(QLatin1String("absent.list")));
    }

    void auth()
    {
        auto *secret = new QLineEdit;
        PnvAuthBlock block(Hy2, QStringLiteral("Password:"), secret);
        QCOMPARE(secret->parentWidget(), &block);
        QCOMPARE(block.findChild<QLabel *>(QStringLiteral("secretLabel"))->buddy(), secret);
        NMStringMap data = map({{"server", "a"}});
        block.write(data);
        QCOMPARE(data, map({{"server", "a"}}));
        QVERIFY(block.isValid());
    }

    void everyValidatorCodeHasAMessage()
    {
        for (int c = PNV_CODES_VALIDATOR_FIRST; c <= PNV_CODES_VALIDATOR_LAST; c++)
            QVERIFY2(pnvHasMessage(PnvCode(c)), pnv_code_name(PnvCode(c)));
    }

    void codeWithoutMessage()
    {
        PnvProblem problem{PnvCode(PNV_CODES_VALIDATOR_LAST + 1000), nullptr, nullptr, 0, 0, 0};
        QCOMPARE(pnvProblemMessage(problem), QString::fromLatin1(pnv_code_name(problem.code)));
    }
};

QTEST_MAIN(TestBlocks)
#include "test_blocks.moc"
