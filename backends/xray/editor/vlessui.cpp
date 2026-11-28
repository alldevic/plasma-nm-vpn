// SPDX-License-Identifier: MIT
// The VLESS XHTTP Reality editor plugin (6.4). plasma-nm's stock import loads every VPN plugin, so the plugin itself
// does nothing until asked for a widget (the value lists are read then), and it declines import and export.
#include "vpnuiplugin.h"

#include "vlesswidget.h"

class VlessUiPlugin : public VpnUiPlugin
{
    Q_OBJECT
public:
    using VpnUiPlugin::VpnUiPlugin;

    SettingWidget *widget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent) override
    {
        return new VlessWidget(setting, parent);
    }

    SettingWidget *askUser(const NetworkManager::VpnSetting::Ptr &setting, const QStringList &hints, QWidget *parent) override
    {
        return new PnvAuthWidget(setting, hints, vlessPromptSecrets(), parent);
    }

    QString suggestedFileName(const NetworkManager::ConnectionSettings::Ptr &) const override
    {
        return {};
    }

    QStringList supportedFileExtensions() const override
    {
        return {};
    }
};

K_PLUGIN_CLASS_WITH_JSON(VlessUiPlugin, "plasmanetworkmanagement_pnv_vlessui.json")

#include "vlessui.moc"
