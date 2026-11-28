// SPDX-License-Identifier: MIT
#include "importwidget.h"

#include <KLocalizedString>
#include <KMessageWidget>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "messages.h"

PnvImportWidget::PnvImportWidget(const QString &service, const QList<ListFile> &lists, Apply apply, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
    , m_lists(lists)
    , m_apply(std::move(apply))
    , m_paste(new QPlainTextEdit(this))
    , m_message(new KMessageWidget(this))
{
    m_paste->setObjectName(QStringLiteral("importText"));
    m_paste->setPlaceholderText(i18nc("@info:placeholder", "Paste a link or a configuration here"));
    m_paste->setMaximumHeight(m_paste->fontMetrics().lineSpacing() * 4);
    auto *importButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-import")),
                                         i18nc("@action:button", "Import"), this);
    importButton->setObjectName(QStringLiteral("importButton"));
    auto *openButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")),
                                       i18nc("@action:button", "Open File…"), this);
    openButton->setObjectName(QStringLiteral("openButton"));
    m_message->setObjectName(QStringLiteral("importMessage"));
    m_message->setCloseButtonVisible(true);
    m_message->setWordWrap(true);
    m_message->hide();

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(importButton);
    buttons->addWidget(openButton);
    buttons->addStretch();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_paste);
    layout->addLayout(buttons);
    layout->addWidget(m_message);

    connect(importButton, &QPushButton::clicked, this, [this] {
        importText(m_paste->toPlainText());
    });
    connect(openButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, i18nc("@title:window", "Open a Link or Configuration"),
                                                          {}, i18nc("@item:inlistbox file filter", "All files (*)"));
        if (!path.isEmpty())
            importFile(path);
    });
}

QString PnvImportWidget::entryName(const QString &service)
{
    if (service == QLatin1String(PNV_SERVICE_HYSTERIA2))
        return QStringLiteral("Hysteria2");
    if (service == QLatin1String(PNV_SERVICE_VLESS))
        return QStringLiteral("VLESS XHTTP Reality");
    if (service == QLatin1String(PNV_SERVICE_AMNEZIAWG))
        return QStringLiteral("AmneziaWG");
    return service;
}

QString PnvImportWidget::message() const
{
    return m_message->isVisibleTo(this) ? m_message->text() : QString();
}

void PnvImportWidget::show(int type, const QString &title, const QStringList &items)
{
    QString text = title.toHtmlEscaped();
    if (!items.isEmpty()) {
        text += QStringLiteral("<ul>");
        for (const QString &item : items)
            text += QStringLiteral("<li>%1</li>").arg(item.toHtmlEscaped());
        text += QStringLiteral("</ul>");
    }
    m_message->setMessageType(KMessageWidget::MessageType(type));
    m_message->setText(text);
    m_message->setVisible(true);
}

bool PnvImportWidget::importText(const QString &text)
{
    return import(text.toUtf8(), true);
}

bool PnvImportWidget::importFile(const QString &path)
{
    QFile file(path);
    // Refused before it is read in full.
    if (file.size() > PNV_IMPORT_MAX_SIZE) {
        PnvProblem problem{PNV_E_IMPORT_TOO_LARGE, nullptr, nullptr, 0, 0, PNV_IMPORT_MAX_SIZE};
        show(KMessageWidget::Error, pnvProblemMessage(problem));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        show(KMessageWidget::Error, i18nc("@info", "Cannot read %1: %2", path, file.errorString()));
        return false;
    }
    return import(file.read(PNV_IMPORT_MAX_SIZE + 1), false);
}

bool PnvImportWidget::import(const QByteArray &text, bool fromPasteField)
{
    // A NUL would cut the text short for the C parser: it is not valid input anyway.
    if (text.contains('\0')) {
        PnvProblem problem{PNV_E_IMPORT_FORMAT, nullptr, nullptr, 0, 0, 0};
        show(KMessageWidget::Error, pnvProblemMessage(problem));
        return false;
    }
    const char *service = pnv_import_service(text.constData());
    if (service && m_service != QLatin1String(service)) {
        show(KMessageWidget::Error,
             i18nc("@info", "This is a %1 connection: add it as “%1” in the list of VPN types.",
                   entryName(QLatin1String(service))));
        return false;
    }
    g_autoptr(PnvList) lists = pnv_list_new();
    for (const ListFile &file : std::as_const(m_lists)) {
        g_auto(PnvProblem) problem = {};
        if (!pnv_list_merge_file(lists, file.path.toUtf8().constData(), file.prefix, &problem)) {
            show(KMessageWidget::Error, pnvProblemMessage(problem));
            return false;
        }
    }
    g_auto(PnvProblem) problem = {};
    g_autoptr(PnvImport) result = pnv_import(text.constData(), lists, &problem);
    if (!result) {
        show(KMessageWidget::Error, i18nc("@info", "Nothing was imported:"), {pnvProblemMessage(problem)});
        return false;
    }
    NMStringMap data, secrets;
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, result->data);
    while (g_hash_table_iter_next(&it, &key, &value))
        data.insert(QString::fromUtf8(static_cast<char *>(key)), QString::fromUtf8(static_cast<char *>(value)));
    g_hash_table_iter_init(&it, result->secrets);
    while (g_hash_table_iter_next(&it, &key, &value))
        secrets.insert(QString::fromUtf8(static_cast<char *>(key)), QString::fromUtf8(static_cast<char *>(value)));
    if (!m_apply(data, secrets))
        return false;
    if (fromPasteField)
        m_paste->clear();
    QStringList warnings;
    for (guint i = 0; i < result->warnings->len; i++)
        warnings << pnvProblemMessage(g_array_index(result->warnings, PnvProblem, i));
    if (warnings.isEmpty())
        show(KMessageWidget::Positive, i18nc("@info", "Imported. Check the fields before saving."));
    else
        show(KMessageWidget::Warning, i18nc("@info", "Imported, with these changes:"), warnings);
    return true;
}
