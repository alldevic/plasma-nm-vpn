// SPDX-License-Identifier: MIT
// The test form for the kill switch banner (6.2), until the editors plug it in: the banner as an editor creates
// it, driven over stdin (clear <uuid> | clear-all | show), printing its state as one JSON line after each command.
// Test build only, run offscreen.
#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <iostream>
#include <string>

#include "killswitchbanner.h"

static void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

static void print(const PnvKillSwitchBanner &banner, const QJsonValue &cleared = {})
{
    QJsonArray entries, actions;
    for (const PnvGuardEntry &e : banner.entries())
        entries.append(e.uuid);
    for (const QAction *a : banner.actions())
        actions.append(a->objectName());
    const QJsonObject state{{QStringLiteral("visible"), banner.isVisibleTo(banner.parentWidget())},
                            {QStringLiteral("entries"), entries},
                            {QStringLiteral("actions"), actions},
                            {QStringLiteral("text"), banner.text()},
                            {QStringLiteral("cleared"), cleared}};
    std::cout << QJsonDocument(state).toJson(QJsonDocument::Compact).constData() << std::endl;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget form;
    auto *banner = new PnvKillSwitchBanner(&form);
    settle(500);
    banner->reload();
    print(*banner);
    for (std::string line; std::getline(std::cin, line);) {
        const QString command = QString::fromStdString(line).trimmed();
        if (command == QLatin1String("show")) {
            settle(500);
            banner->reload();
            print(*banner);
            continue;
        }
        if (command != QLatin1String("clear-all") && !command.startsWith(QLatin1String("clear ")))
            return 2;
        bool cleared = false;
        QEventLoop loop;
        QObject::connect(banner, &PnvKillSwitchBanner::finished, &loop, [&](bool ok) {
            cleared = ok;
            loop.quit();
        });
        banner->clear(command == QLatin1String("clear-all") ? QString() : command.mid(6));
        loop.exec();
        QObject::disconnect(banner, &PnvKillSwitchBanner::finished, &loop, nullptr);
        print(*banner, cleared);
    }
    return 0;
}
