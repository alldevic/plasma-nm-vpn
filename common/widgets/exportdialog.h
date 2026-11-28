// SPDX-License-Identifier: MIT
#pragma once

#include <NetworkManagerQt/GenericTypes>
#include <QDialog>

#include "importwidget.h"
#include "pnv_widgets_export.h"

class KMessageWidget;
class QCheckBox;

// The profile in the format import reads (6.8), and what that format leaves out as rendered messages.
struct PNV_WIDGETS_EXPORT PnvExport {
    QString text;
    QStringList warnings;
};
PNV_WIDGETS_EXPORT PnvExport pnvExport(const QString &service, const NMStringMap &data, const NMStringMap &secrets,
                                       bool withSecrets, const QList<PnvImportWidget::ListFile> &lists);
// Written with mode 0600 when it holds secrets, 0644 otherwise.
PNV_WIDGETS_EXPORT bool pnvWriteExport(const QString &path, const QString &text, bool withSecrets, QString *error);

// "Export": copy the link to the clipboard or save it to a file, secrets only when asked for.
class PNV_WIDGETS_EXPORT PnvExportDialog : public QDialog
{
    Q_OBJECT
public:
    PnvExportDialog(const QString &service, const NMStringMap &data, const NMStringMap &secrets,
                    const QList<PnvImportWidget::ListFile> &lists, QWidget *parent = nullptr);

    PnvExport current() const;
    bool saveTo(const QString &path);

private:
    void refresh();

    QString m_service;
    NMStringMap m_data, m_secrets;
    QList<PnvImportWidget::ListFile> m_lists;
    QCheckBox *m_secretsBox;
    KMessageWidget *m_message;
};
