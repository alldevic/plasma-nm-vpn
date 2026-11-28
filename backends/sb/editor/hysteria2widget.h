// SPDX-License-Identifier: MIT
// The Hysteria2 editor (6.3): the 6.1 blocks, its own obfuscation and port hopping group, the 6.2 banner.
#pragma once

#include "pnv-editor.h"

class QComboBox;
class QLabel;
class QLineEdit;

// "Obfuscation, port hopping": obfs, hop-interval, up-mbps, down-mbps; the obfs-password field is the editor's.
class Hysteria2ObfsBlock : public PnvBlock
{
    Q_OBJECT
public:
    Hysteria2ObfsBlock(PasswordField *obfsPassword, QWidget *parent = nullptr);

    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;
    bool salamander() const;

protected:
    QList<Field> fields() const override;

private:
    QComboBox *m_obfs;
    PasswordField *m_obfsPassword;
    QLineEdit *m_hopInterval, *m_up, *m_down;
    QLabel *m_obfsPasswordLabel, *m_hopLabel, *m_upLabel, *m_downLabel;
};

class Hysteria2Widget : public PnvEditorWidget
{
    Q_OBJECT
public:
    // extraAllowlist: the package's list; tests pass the one in the source tree.
    explicit Hysteria2Widget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent = nullptr,
                             const QString &extraAllowlist = QStringLiteral("/usr/share/plasma-nm-vpn/sb/hysteria2-extra.list"));

protected:
    PasswordField::PasswordOption secretOption(const QString &key, const PasswordField *field) const override;

private:
    Hysteria2ObfsBlock *m_obfs;
};

QList<PnvPromptSecret> hysteria2PromptSecrets();
