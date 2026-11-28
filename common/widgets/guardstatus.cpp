// SPDX-License-Identifier: MIT
#include "guardstatus.h"

#include <QFile>
#include <QHash>
#include <QUuid>

// The version the daemons write (common/daemon/status.c).
static constexpr int Version = 1;

QList<PnvGuardEntry> pnvReadGuardStatus(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList order;
    QHash<QString, QHash<QString, QString>> groups;
    QString group;
    // A group that is not a UUID is skipped, keys and all.
    bool inGroup = false;
    for (const QByteArray &raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            group = line.mid(1, line.size() - 2);
            inGroup = group.size() == 36 && !QUuid::fromString(group).isNull();
            if (inGroup && !groups.contains(group))
                order << group;
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (!inGroup || eq <= 0)
            continue;
        groups[group].insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    QList<PnvGuardEntry> entries;
    for (const QString &uuid : std::as_const(order)) {
        const auto &keys = groups.value(uuid);
        bool ok = false;
        if (keys.value(QStringLiteral("version")).toInt(&ok) != Version || !ok)
            continue;
        if (keys.value(QStringLiteral("state")) != QLatin1String("blocked"))
            continue;
        entries.append({uuid, keys.value(QStringLiteral("reason")), keys.value(QStringLiteral("since")).toLongLong()});
    }
    return entries;
}
