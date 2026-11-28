// SPDX-License-Identifier: MIT
// The AmneziaWG editor plugin (6.5). plasma-nm's stock import loads every VPN plugin, so the plugin itself
// does nothing until asked for a widget (the allowlist is read then), and it declines import and export.
#include "vpnuiplugin.h"

#include "awgwidget.h"

class AwgUiPlugin : public VpnUiPlugin
{
    Q_OBJECT
public:
    using VpnUiPlugin::VpnUiPlugin;

    SettingWidget *widget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent) override
    {
        return new AwgWidget(setting, parent);
    }

    SettingWidget *askUser(const NetworkManager::VpnSetting::Ptr &setting, const QStringList &hints, QWidget *parent) override
    {
        return new PnvAuthWidget(setting, hints, awgPromptSecrets(), parent);
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

K_PLUGIN_CLASS_WITH_JSON(AwgUiPlugin, "plasmanetworkmanagement_pnv_amneziawgui.json")

#include "awgui.moc"
