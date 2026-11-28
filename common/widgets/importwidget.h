// SPDX-License-Identifier: MIT
#pragma once

#include <NetworkManagerQt/GenericTypes>
#include <QWidget>

#include <functional>

#include "pnv_widgets_export.h"

class KMessageWidget;
class QPlainTextEdit;

// "Paste link or config" and "Open File…" (6.7) for an editor: the 6.6 parsers with the editor's lists, the
// result handed to the editor, warnings and errors as rendered codes. A link for another protocol names the entry
// to choose instead. After a successful import the paste field is cleared, so a link with secrets does not stay
// there; after an error it is kept for correction.
class PNV_WIDGETS_EXPORT PnvImportWidget : public QWidget
{
    Q_OBJECT
public:
    struct ListFile {
        const char *prefix;
        QString path;
    };
    // Applies the parsed fields to the form; false if the user declined (nothing changed).
    using Apply = std::function<bool(const NMStringMap &data, const NMStringMap &secrets)>;

    PnvImportWidget(const QString &service, const QList<ListFile> &lists, Apply apply, QWidget *parent = nullptr);

    // What the buttons do, for tests.
    bool importText(const QString &text);
    bool importFile(const QString &path);
    QString message() const;

    // The "Add VPN" entry of a service, as the editors' metadata names it.
    static QString entryName(const QString &service);

private:
    bool import(const QByteArray &text, bool fromPasteField);
    void show(int type, const QString &title, const QStringList &items = {});

    QString m_service;
    QList<ListFile> m_lists;
    Apply m_apply;
    QPlainTextEdit *m_paste;
    KMessageWidget *m_message;
};
