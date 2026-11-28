// SPDX-License-Identifier: MIT
// Helpers of the blocks, not exported.
#pragma once

#include <NetworkManagerQt/GenericTypes>
#include <QLabel>

// An empty value is the same as an absent one (design.md, "Validation").
inline void pnvSet(NMStringMap &data, const QString &key, const QString &value)
{
    if (value.isEmpty())
        data.remove(key);
    else
        data.insert(key, value);
}

inline void pnvSetBool(NMStringMap &data, const QString &key, bool value)
{
    data.insert(key, value ? QStringLiteral("yes") : QStringLiteral("no"));
}

// "Server:" with its accelerator → "Server", the field's name in messages.
inline QString pnvLabel(const QLabel *label)
{
    QString text = label->text().remove(QLatin1Char('&'));
    if (text.endsWith(QLatin1Char(':')))
        text.chop(1);
    return text;
}
