// SPDX-License-Identifier: MIT
// The Hysteria2 editor (6.3): the 6.1 blocks, its own obfuscation and port hopping group, the 6.2 banner.
#pragma once

#include "block.h"
#include "settingwidget.h"

#include <NetworkManagerQt/VpnSetting>

class PasswordField;
class PnvKillSwitchBanner;
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

class Hysteria2Widget : public SettingWidget
{
    Q_OBJECT
public:
    // extraAllowlist: the package's list; tests pass the one in the source tree.
    explicit Hysteria2Widget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent = nullptr,
                             const QString &extraAllowlist = QStringLiteral("/usr/share/plasma-nm-vpn/sb/hysteria2-extra.list"));

    void loadConfig(const NetworkManager::Setting::Ptr &setting) override;
    void loadSecrets(const NetworkManager::Setting::Ptr &setting) override;
    QVariantMap setting() const override;
    bool isValid() const override;

private:
    QList<PnvBlock *> m_blocks;
    PasswordField *m_password, *m_obfsPassword;
    Hysteria2ObfsBlock *m_obfs;
};

// The prompt kded6 shows for a secret asked every time or rejected.
class Hysteria2AuthWidget : public SettingWidget
{
    Q_OBJECT
public:
    Hysteria2AuthWidget(const NetworkManager::VpnSetting::Ptr &setting, const QStringList &hints, QWidget *parent = nullptr);

    QVariantMap setting() const override;
    bool isValid() const override;

private:
    QList<QPair<QString, PasswordField *>> m_fields;
};
