// SPDX-License-Identifier: MIT
#pragma once

#include <KMessageWidget>

#include <functional>

#include "guardstatus.h"
#include "pnv_widgets_export.h"

class QProcess;

// "Kill switch block active" (design.md, "How the user learns about a block"). The editor does not know which
// profile it edits, so the banner lists every block, by profile name, each with its own Clear; Clear all with
// more than one. Clearing runs plasma-nm-vpn-guard, which asks polkit for administrator credentials itself.
// The file is read when the banner is created and after each clear, not watched.
class PNV_WIDGETS_EXPORT PnvKillSwitchBanner : public KMessageWidget
{
    Q_OBJECT
public:
    enum class State { Down, Connecting, Connected };
    using NameLookup = std::function<QString(const QString &uuid)>;
    using StateLookup = std::function<State(const QString &uuid)>;

    explicit PnvKillSwitchBanner(QWidget *parent = nullptr);
    // For tests: another file, a stub for the tool, lookups without NetworkManager. Each call reloads.
    PnvKillSwitchBanner(const QString &statusPath, const QString &guardProgram, NameLookup names, StateLookup states,
                        QWidget *parent = nullptr);
    ~PnvKillSwitchBanner() override;

    QList<PnvGuardEntry> entries() const;
    // An empty uuid clears all; finished() follows once the tool has exited and the file is read again.
    void clear(const QString &uuid = {});
    void reload();

Q_SIGNALS:
    void finished(bool cleared);

private:
    void refresh();
    QString describe(const PnvGuardEntry &entry) const;

    QString m_statusPath, m_guardProgram;
    NameLookup m_names;
    StateLookup m_states;
    QList<PnvGuardEntry> m_entries;
    QString m_failure;
    QProcess *m_process = nullptr;
};
