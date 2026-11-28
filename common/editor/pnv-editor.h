// SPDX-License-Identifier: MIT
// What the editors in the core packages share (6.3–6.5): the form with the kill switch banner and the groups of
// blocks, the secret fields, saving into the vpn setting, and the prompt kded6 shows. It uses plasma-nm's API, so
// it is built into each editor, not into the widget library.
#pragma once

#include <NetworkManagerQt/VpnSetting>

#include "authblock.h"
#include "block.h"
#include "passwordfield.h"
#include "settingwidget.h"

class QLabel;
class QVBoxLayout;

QLabel *pnvBuddyLabel(const QString &text, QWidget *buddy);

class PnvEditorWidget : public SettingWidget
{
    Q_OBJECT
public:
    PnvEditorWidget(const QString &service, const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent);

    void loadConfig(const NetworkManager::Setting::Ptr &setting) override;
    void loadSecrets(const NetworkManager::Setting::Ptr &setting) override;
    QVariantMap setting() const override;
    bool isValid() const override;

protected:
    // A titled group of blocks, in the order of the calls.
    void addGroup(const QString &title, const QList<PnvBlock *> &blocks);
    // A masked secret field with the storage choices; optional ones also offer "not required".
    PasswordField *secretField(const QString &key, bool optional);
    // The "Authentication" block for a secret field, checking its value.
    PnvAuthBlock *authBlock(const QString &label, const QString &key, PasswordField *field);
    // The storage written for a secret; editors override it where another field decides.
    virtual PasswordField::PasswordOption secretOption(const QString &key, const PasswordField *field) const;
    // Called at the end of the subclass constructor, once the fields exist.
    void finish(const NetworkManager::VpnSetting::Ptr &setting);

    QString m_service;

private:
    QVBoxLayout *m_content;
    QList<PnvBlock *> m_blocks;
    QList<QPair<QString, PasswordField *>> m_secrets;
};

// The prompt kded6 shows for a secret asked every time or rejected: the secrets NM names in its hints, otherwise
// those neither stored nor marked not required.
struct PnvPromptSecret {
    QString key;
    QString label;
    QString onlyWith; // asked only if this vpn.data key is set
};

class PnvAuthWidget : public SettingWidget
{
    Q_OBJECT
public:
    PnvAuthWidget(const NetworkManager::VpnSetting::Ptr &setting, const QStringList &hints,
                  const QList<PnvPromptSecret> &secrets, QWidget *parent);

    QVariantMap setting() const override;
    bool isValid() const override;

private:
    QList<QPair<QString, PasswordField *>> m_fields;
};
