// SPDX-License-Identifier: MIT
// Message codes (pnv-profile.h) rendered through ki18n: one table for the validators, the import parsers and the
// export, each group filled in by its milestone.
#pragma once

#include <QString>

#include "pnv-profile.h"
#include "pnv_widgets_export.h"

// The field is named by label if given, otherwise by its key. A code without an entry is shown as its name.
PNV_WIDGETS_EXPORT QString pnvProblemMessage(const PnvProblem &problem, const QString &label = {});
PNV_WIDGETS_EXPORT bool pnvHasMessage(PnvCode code);
