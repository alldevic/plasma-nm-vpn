// SPDX-License-Identifier: MIT
#include "exportdialog.h"

#include <KLocalizedString>
#include <KMessageWidget>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "messages.h"

static GHashTable *table(const NMStringMap &map)
{
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        g_hash_table_insert(t, g_strdup(it.key().toUtf8().constData()), g_strdup(it.value().toUtf8().constData()));
    return t;
}

static void clearWarning(gpointer w)
{
    pnv_problem_clear(static_cast<PnvProblem *>(w));
}

PnvExport pnvExport(const QString &service, const NMStringMap &data, const NMStringMap &secrets, bool withSecrets,
                    const QList<PnvImportWidget::ListFile> &lists)
{
    g_autoptr(GHashTable) d = table(data);
    g_autoptr(GHashTable) s = table(secrets);
    g_autoptr(PnvList) merged = pnv_list_new();
    g_autoptr(GArray) warnings = g_array_new(FALSE, TRUE, sizeof(PnvProblem));
    g_array_set_clear_func(warnings, clearWarning);
    for (const auto &file : lists)
        pnv_list_merge_file(merged, file.path.toUtf8().constData(), file.prefix, nullptr);
    g_autofree char *text = pnv_export(service.toUtf8().constData(), d, withSecrets ? s : nullptr, merged, warnings);
    PnvExport out{QString::fromUtf8(text ? text : ""), {}};
    for (guint i = 0; i < warnings->len; i++)
        out.warnings << pnvProblemMessage(g_array_index(warnings, PnvProblem, i));
    return out;
}

bool pnvWriteExport(const QString &path, const QString &text, bool withSecrets, QString *error)
{
    const QByteArray name = QFile::encodeName(path), bytes = text.toUtf8();
    const mode_t mode = withSecrets ? 0600 : 0644;
    // Created with its mode, and an existing file narrowed too, before anything is written.
    const int fd = ::open(name.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
    bool ok = fd >= 0 && fchmod(fd, mode) == 0;
    for (qsizetype done = 0; ok && done < bytes.size();) {
        const ssize_t n = ::write(fd, bytes.constData() + done, bytes.size() - done);
        ok = n > 0;
        done += ok ? n : 0;
    }
    if (!ok && error)
        *error = QString::fromLocal8Bit(strerror(errno));
    if (fd >= 0 && ::close(fd) != 0)
        ok = false;
    return ok;
}

PnvExportDialog::PnvExportDialog(const QString &service, const NMStringMap &data, const NMStringMap &secrets,
                                 const QList<PnvImportWidget::ListFile> &lists, QWidget *parent)
    : QDialog(parent)
    , m_service(service)
    , m_data(data)
    , m_secrets(secrets)
    , m_lists(lists)
    , m_secretsBox(new QCheckBox(i18nc("@option:check", "Include secrets"), this))
    , m_message(new KMessageWidget(this))
{
    setWindowTitle(i18nc("@title:window", "Export %1", PnvImportWidget::entryName(service)));
    m_secretsBox->setObjectName(QStringLiteral("includeSecrets"));
    auto *note = new QLabel(i18nc("@info", "A link copied with its secrets may stay in the clipboard history."), this);
    note->setWordWrap(true);
    m_message->setObjectName(QStringLiteral("exportMessage"));
    m_message->setMessageType(KMessageWidget::Warning);
    m_message->setCloseButtonVisible(false);
    m_message->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = buttons->addButton(i18nc("@action:button", "Copy to Clipboard"), QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("copyButton"));
    copy->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    auto *save = buttons->addButton(i18nc("@action:button", "Save to File…"), QDialogButtonBox::ActionRole);
    save->setObjectName(QStringLiteral("saveButton"));
    save->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_secretsBox);
    layout->addWidget(note);
    layout->addWidget(m_message);
    layout->addWidget(buttons);

    connect(m_secretsBox, &QCheckBox::toggled, this, &PnvExportDialog::refresh);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(current().text);
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const bool conf = m_service == QLatin1String(PNV_SERVICE_AMNEZIAWG);
        const QString path = QFileDialog::getSaveFileName(
            this, windowTitle(), conf ? QStringLiteral("amneziawg.conf") : QStringLiteral("link.txt"),
            conf ? i18nc("@item:inlistbox file filter", "Configuration (*.conf)") : i18nc("@item:inlistbox file filter", "Text (*.txt)"));
        if (!path.isEmpty())
            saveTo(path);
    });
    refresh();
}

PnvExport PnvExportDialog::current() const
{
    return pnvExport(m_service, m_data, m_secrets, m_secretsBox->isChecked(), m_lists);
}

void PnvExportDialog::refresh()
{
    const QStringList warnings = current().warnings;
    QString text = i18nc("@info", "Not in the export:").toHtmlEscaped() + QStringLiteral("<ul>");
    for (const QString &w : warnings)
        text += QStringLiteral("<li>%1</li>").arg(w.toHtmlEscaped());
    m_message->setText(text + QStringLiteral("</ul>"));
    m_message->setVisible(!warnings.isEmpty());
}

bool PnvExportDialog::saveTo(const QString &path)
{
    QString error;
    if (pnvWriteExport(path, current().text, m_secretsBox->isChecked(), &error))
        return true;
    m_message->setMessageType(KMessageWidget::Error);
    m_message->setText(i18nc("@info", "Cannot write %1: %2", path, error).toHtmlEscaped());
    m_message->setVisible(true);
    return false;
}
