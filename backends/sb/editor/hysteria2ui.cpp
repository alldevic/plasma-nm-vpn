// SPDX-License-Identifier: MIT
// The Hysteria2 editor plugin (6.3). plasma-nm's stock import loads every VPN plugin, so the plugin itself does
// nothing until asked for a widget, and it declines import and export.
#include "vpnuiplugin.h"

#include "hysteria2widget.h"

class Hysteria2UiPlugin : public VpnUiPlugin
{
    Q_OBJECT
public:
    using VpnUiPlugin::VpnUiPlugin;

    SettingWidget *widget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent) override
    {
        return new Hysteria2Widget(setting, parent);
    }

    SettingWidget *askUser(const NetworkManager::VpnSetting::Ptr &setting, const QStringList &hints, QWidget *parent) override
    {
        return new Hysteria2AuthWidget(setting, hints, parent);
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

K_PLUGIN_CLASS_WITH_JSON(Hysteria2UiPlugin, "plasmanetworkmanagement_pnv_hysteria2ui.json")

#include "hysteria2ui.moc"
