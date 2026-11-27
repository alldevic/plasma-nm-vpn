// SPDX-License-Identifier: MIT
#include "vpnuiplugin.h"

static const QString Service = QStringLiteral("org.freedesktop.NetworkManager.pnv.hysteria2");

class Hysteria2Widget : public SettingWidget
{
    Q_OBJECT
public:
    using SettingWidget::SettingWidget;

    QVariantMap setting() const override
    {
        NetworkManager::VpnSetting vpn;
        vpn.setServiceType(Service);
        return vpn.toMap();
    }
};

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
        return new Hysteria2Widget(setting, hints, parent);
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
