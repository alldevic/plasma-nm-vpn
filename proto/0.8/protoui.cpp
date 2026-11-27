// Minimal plasma-nm VPN editor for milestone 0.8: one field, "server", in vpn.data.
#include "vpnuiplugin.h"

#include <QFormLayout>
#include <QLineEdit>

static const QString Service = QStringLiteral("org.freedesktop.NetworkManager.pnv-proto");

class ProtoWidget : public SettingWidget
{
    Q_OBJECT
public:
    ProtoWidget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent)
        : SettingWidget(setting, parent)
        , m_server(new QLineEdit(this))
    {
        auto *layout = new QFormLayout(this);
        m_server->setObjectName(QStringLiteral("server"));
        layout->addRow(QStringLiteral("Server:"), m_server);
        if (setting) {
            loadConfig(setting);
        }
        watchChangedSetting();
    }

    void loadConfig(const NetworkManager::Setting::Ptr &setting) override
    {
        m_server->setText(setting.staticCast<NetworkManager::VpnSetting>()->data().value(QStringLiteral("server")));
    }

    QVariantMap setting() const override
    {
        NetworkManager::VpnSetting vpn;
        NMStringMap data;
        data.insert(QStringLiteral("server"), m_server->text());
        vpn.setServiceType(Service);
        vpn.setData(data);
        return vpn.toMap();
    }

private:
    QLineEdit *m_server;
};

class EmptyWidget : public SettingWidget
{
    Q_OBJECT
public:
    using SettingWidget::SettingWidget;
    QVariantMap setting() const override
    {
        return {};
    }
};

class ProtoUiPlugin : public VpnUiPlugin
{
    Q_OBJECT
public:
    ProtoUiPlugin(QObject *parent, const QVariantList &args)
        : VpnUiPlugin(parent, args)
    {
    }

    SettingWidget *widget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent) override
    {
        return new ProtoWidget(setting, parent);
    }

    SettingWidget *askUser(const NetworkManager::VpnSetting::Ptr &setting, const QStringList &hints, QWidget *parent) override
    {
        return new EmptyWidget(setting, hints, parent);
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

K_PLUGIN_CLASS_WITH_JSON(ProtoUiPlugin, "plasmanetworkmanagement_pnvprotoui.json")

#include "protoui.moc"
