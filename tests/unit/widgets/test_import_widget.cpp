// SPDX-License-Identifier: MIT
// The import widget (6.7): what it hands to the editor, the paste field after success and failure, a link for
// another protocol, parser errors and warnings as rendered messages, the file size limit.
#include <KMessageWidget>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include "importwidget.h"
#include "pnv-profile.h"

static const QString Source = QStringLiteral(PNV_SOURCE_DIR);

static QString testData(const char *name)
{
    QFile f(Source + QStringLiteral("/tests/unit/data/import/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("no test data %s", name);
    return QString::fromUtf8(f.readAll());
}

class TestImportWidget : public QObject
{
    Q_OBJECT

    NMStringMap gotData, gotSecrets;
    int calls = 0;
    bool accept = true;

    PnvImportWidget *hysteria2(QWidget *parent)
    {
        return new PnvImportWidget(QStringLiteral(PNV_SERVICE_HYSTERIA2),
                                   {{"extra/", Source + QStringLiteral("/backends/sb/data/hysteria2-extra.list")}},
                                   [this](const NMStringMap &data, const NMStringMap &secrets) {
                                       calls++;
                                       gotData = data;
                                       gotSecrets = secrets;
                                       return accept;
                                   },
                                   parent);
    }

    static void paste(PnvImportWidget *w, const QString &text)
    {
        w->findChild<QPlainTextEdit *>(QStringLiteral("importText"))->setPlainText(text);
        w->findChild<QPushButton *>(QStringLiteral("importButton"))->click();
    }

    static QString pasted(PnvImportWidget *w)
    {
        return w->findChild<QPlainTextEdit *>(QStringLiteral("importText"))->toPlainText();
    }

private Q_SLOTS:
    void init()
    {
        calls = 0;
        accept = true;
        gotData.clear();
        gotSecrets.clear();
    }

    void pasteFillsAndClears()
    {
        QWidget form;
        PnvImportWidget *w = hysteria2(&form);
        paste(w, testData("bench-hysteria2.txt"));
        QCOMPARE(calls, 1);
        QCOMPARE(gotData.value(QStringLiteral("server")), QStringLiteral("srv.stand.test"));
        QCOMPARE(gotSecrets.value(QStringLiteral("password")), QStringLiteral("4cfe6b51c9a1f0d2e7b38a6c5d4e3f21"));
        QVERIFY(pasted(w).isEmpty());
        QVERIFY(w->message().contains(QLatin1String("Imported")));
    }

    void declinedKeepsTheText()
    {
        QWidget form;
        PnvImportWidget *w = hysteria2(&form);
        accept = false;
        paste(w, testData("bench-hysteria2.txt"));
        QCOMPARE(calls, 1);
        QVERIFY(!pasted(w).isEmpty());
    }

    void otherProtocol()
    {
        QWidget form;
        PnvImportWidget *w = hysteria2(&form);
        paste(w, testData("bench-vless.txt"));
        QCOMPARE(calls, 0);
        QVERIFY(!pasted(w).isEmpty());
        QVERIFY2(w->message().contains(QLatin1String("VLESS XHTTP Reality")), qPrintable(w->message()));
    }

    void errorsAreRenderedAndKeepTheText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("gecko") << "hy2://x@h.example:443?obfs=gecko" << "gecko is not supported in this version";
        QTest::newRow("realm") << "hysteria2+realm://x@h.example:443" << "hysteria2+realm is not supported";
        QTest::newRow("format") << "ss://x" << "This is not a hysteria2://";
        QTest::newRow("validator") << "hy2://x@h.example:99999" << "not a list of ports";
    }

    void errorsAreRenderedAndKeepTheText()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QWidget form;
        PnvImportWidget *w = hysteria2(&form);
        paste(w, text);
        QCOMPARE(calls, 0);
        QCOMPARE(pasted(w), text);
        QVERIFY2(w->message().contains(expected), qPrintable(w->message()));
        QVERIFY(!w->message().contains(QLatin1String("import-")));
    }

    void warningsAreListed()
    {
        QWidget form;
        PnvImportWidget *w = hysteria2(&form);
        paste(w, QStringLiteral("hy2://x@h.example:443?upmbps=10&udp_fragment=true"));
        QCOMPARE(calls, 1);
        QVERIFY(w->message().contains(QLatin1String("upmbps is not supported here and was dropped")));
        QVERIFY(w->message().contains(QLatin1String("udp_fragment was carried into the additional parameters")));
        QCOMPARE(w->findChild<KMessageWidget *>()->messageType(), KMessageWidget::Warning);
    }

    void files()
    {
        QTemporaryDir dir;
        QFile big(dir.filePath(QStringLiteral("big.conf")));
        QVERIFY(big.open(QIODevice::WriteOnly));
        big.write(QByteArray(PNV_IMPORT_MAX_SIZE + 1, '#'));
        big.close();
        QWidget form;
        auto *awg = new PnvImportWidget(QStringLiteral(PNV_SERVICE_AMNEZIAWG),
                                        {{"awg/", Source + QStringLiteral("/backends/awg/data/amneziawg-params.list")}},
                                        [this](const NMStringMap &data, const NMStringMap &) {
                                            calls++;
                                            gotData = data;
                                            return true;
                                        },
                                        &form);
        QVERIFY(!awg->importFile(big.fileName()));
        QVERIFY(awg->message().contains(QLatin1String("is larger than")));
        QVERIFY(awg->importFile(Source + QStringLiteral("/tests/unit/data/import/bench-amneziawg.conf")));
        QCOMPARE(gotData.value(QStringLiteral("awg-jc")), QStringLiteral("4"));
    }
};

QTEST_MAIN(TestImportWidget)
#include "test_import_widget.moc"
