// SPDX-License-Identifier: MIT
// /run/plasma-nm-vpn/guard-status (design.md, "How the user learns about a block"), as the banner reads it.
#pragma once

#include <QList>
#include <QString>

#include "pnv_widgets_export.h"

struct PnvGuardEntry {
    QString uuid;
    QString reason; // gave-up, daemon-gone, inherited, or what a newer daemon writes
    qint64 since = 0;
};

// The blocks in the file, in file order. A missing file means none. Lines that are not a group, a key=value or a
// comment are skipped, and so are groups that are not a UUID, of another version, or in another state than blocked.
PNV_WIDGETS_EXPORT QList<PnvGuardEntry> pnvReadGuardStatus(const QString &path);
