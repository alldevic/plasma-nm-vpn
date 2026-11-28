// SPDX-License-Identifier: MIT
#include "killswitchbanner.h"

#include <KLocalizedString>
#include <NetworkManagerQt/ActiveConnection>
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/Settings>
#include <QAction>
#include <QDateTime>
#include <QLocale>
#include <QProcess>

// The paths of plasma-nm-vpn-common (common/daemon/pnv-daemon.h).
static const QString StatusPath = QStringLiteral("/run/plasma-nm-vpn/guard-status");
static const QString GuardProgram = QStringLiteral("/usr/bin/plasma-nm-vpn-guard");

static QString nmName(const QString &uuid)
{
    const NetworkManager::Connection::Ptr connection = NetworkManager::findConnectionByUuid(uuid);
    return connection ? connection->name() : QString();
}

static PnvKillSwitchBanner::State nmState(const QString &uuid)
{
    for (const NetworkManager::ActiveConnection::Ptr &ac : NetworkManager::activeConnections()) {
        if (ac->uuid() != uuid)
            continue;
        if (ac->state() == NetworkManager::ActiveConnection::Activated)
            return PnvKillSwitchBanner::State::Connected;
        if (ac->state() == NetworkManager::ActiveConnection::Activating)
            return PnvKillSwitchBanner::State::Connecting;
    }
    return PnvKillSwitchBanner::State::Down;
}

PnvKillSwitchBanner::PnvKillSwitchBanner(QWidget *parent)
    : PnvKillSwitchBanner(StatusPath, GuardProgram, nmName, nmState, parent)
{
}

PnvKillSwitchBanner::PnvKillSwitchBanner(const QString &statusPath, const QString &guardProgram, NameLookup names,
                                         StateLookup states, QWidget *parent)
    : KMessageWidget(parent)
    , m_statusPath(statusPath)
    , m_guardProgram(guardProgram)
    , m_names(std::move(names))
    , m_states(std::move(states))
{
    setMessageType(KMessageWidget::Warning);
    setCloseButtonVisible(false);
    setWordWrap(true);
    reload();
}

PnvKillSwitchBanner::~PnvKillSwitchBanner() = default;

QList<PnvGuardEntry> PnvKillSwitchBanner::entries() const
{
    return m_entries;
}

void PnvKillSwitchBanner::reload()
{
    m_entries = pnvReadGuardStatus(m_statusPath);
    refresh();
}

QString PnvKillSwitchBanner::describe(const PnvGuardEntry &entry) const
{
    const QString name = m_names(entry.uuid);
    QString text = (name.isEmpty() ? entry.uuid : name).toHtmlEscaped();
    QString why;
    if (entry.reason == QLatin1String("gave-up"))
        why = i18nc("@info why a block stayed", "the connection could not be restored");
    else if (entry.reason == QLatin1String("daemon-gone"))
        why = i18nc("@info why a block stayed", "the VPN service stopped unexpectedly");
    else if (entry.reason == QLatin1String("inherited"))
        why = i18nc("@info why a block stayed", "the last connection attempt did not succeed");
    if (!why.isEmpty())
        text = i18nc("@info profile name, reason", "%1: %2", text, why);
    if (entry.since > 0) {
        const QString since = QLocale().toString(QDateTime::fromSecsSinceEpoch(entry.since), QLocale::ShortFormat);
        text = i18nc("@info a block, the time it began", "%1, since %2", text, since);
    }
    switch (m_states(entry.uuid)) {
    case State::Connecting:
        return i18nc("@info a block", "%1 (connecting)", text);
    case State::Connected:
        return i18nc("@info a block", "%1 (connected)", text);
    case State::Down:
        break;
    }
    return text;
}

void PnvKillSwitchBanner::refresh()
{
    for (QAction *action : actions())
        removeAction(action);
    setVisible(!m_entries.isEmpty());
    if (m_entries.isEmpty())
        return;
    QString text = QStringLiteral("<b>%1</b><br>%2<ul>")
                       .arg(i18nc("@title", "Kill switch block active"),
                            i18nc("@info", "Traffic outside the VPN is blocked for:"));
    for (const PnvGuardEntry &entry : std::as_const(m_entries)) {
        text += QStringLiteral("<li>%1</li>").arg(describe(entry));
        const QString name = m_names(entry.uuid);
        auto *action = new QAction(i18nc("@action:button", "Clear “%1”", name.isEmpty() ? entry.uuid : name), this);
        action->setObjectName(QStringLiteral("clear-") + entry.uuid);
        connect(action, &QAction::triggered, this, [this, uuid = entry.uuid] {
            clear(uuid);
        });
        addAction(action);
    }
    text += QStringLiteral("</ul>");
    if (m_entries.size() > 1) {
        auto *all = new QAction(i18nc("@action:button", "Clear All"), this);
        all->setObjectName(QStringLiteral("clear-all"));
        all->setToolTip(i18nc("@info:tooltip", "Connections that are connected or connecting keep their block"));
        connect(all, &QAction::triggered, this, [this] {
            clear();
        });
        addAction(all);
    }
    if (!m_failure.isEmpty())
        text += m_failure.toHtmlEscaped();
    setText(text);
}

void PnvKillSwitchBanner::clear(const QString &uuid)
{
    if (m_process)
        return;
    QStringList args{QStringLiteral("--clear")};
    if (!uuid.isEmpty())
        args << uuid;
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        m_process->deleteLater();
        m_process = nullptr;
        m_failure = ok ? QString() : i18nc("@info", "The block was not cleared: authentication was refused or failed.");
        reload();
        Q_EMIT finished(ok);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        m_failure = i18nc("@info", "The block was not cleared: %1 could not be started.", m_guardProgram);
        reload();
        Q_EMIT finished(false);
    });
    m_process->start(m_guardProgram, args);
}
