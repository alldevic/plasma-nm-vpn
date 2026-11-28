// SPDX-License-Identifier: MIT
// The test driver for the editors (6.3–6.5): loads the editor plugin of a service the way plasma-nm does, fills its
// fields by object name, and saves the profile into NM through ConnectionSettings, as plasma-nm's connection editor
// does. Test build only, run offscreen.
//   editor-form <service> new <name> [<object>=<value> | <field>:option=<option> ...]
//   editor-form <service> edit <uuid> [...]
// A value goes into the QLineEdit, QPlainTextEdit, PasswordField, QCheckBox (yes/no) or QComboBox (item data) of
// that name; click=<button> presses a button. Options: user, all, ask, none. Prints one JSON line: valid, problems, uuid.
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/ConnectionSettings>
#include <NetworkManagerQt/Settings>
#include <NetworkManagerQt/VpnSetting>
#include <KMessageWidget>
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusMetaType>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>

#include <iostream>

#include "passwordfield.h"
#include "vpnuiplugin.h"

static int fail(const QString &why)
{
    std::cerr << why.toStdString() << std::endl;
    return 2;
}

static bool set(QWidget *form, const QString &name, const QString &value)
{
    // click=<button>: e.g. a table's Add button, before its new row's fields are set.
    if (name == QLatin1String("click")) {
        auto *button = form->findChild<QAbstractButton *>(value);
        return button && button->isEnabled() && (button->click(), true);
    }
    if (auto *f = form->findChild<PasswordField *>(name))
        return f->setText(value), true;
    if (auto *e = form->findChild<QLineEdit *>(name))
        return e->setText(value), true;
    if (auto *t = form->findChild<QPlainTextEdit *>(name))
        return t->setPlainText(value), true;
    if (auto *c = form->findChild<QCheckBox *>(name))
        return c->setChecked(value == QLatin1String("yes")), true;
    if (auto *b = form->findChild<QComboBox *>(name)) {
        const int index = b->findData(value);
        return index >= 0 && (b->setCurrentIndex(index), true);
    }
    return false;
}

static bool option(QWidget *form, const QString &name, const QString &value)
{
    static const QHash<QString, PasswordField::PasswordOption> options{{QStringLiteral("user"), PasswordField::StoreForUser},
                                                                      {QStringLiteral("all"), PasswordField::StoreForAllUsers},
                                                                      {QStringLiteral("ask"), PasswordField::AlwaysAsk},
                                                                      {QStringLiteral("none"), PasswordField::NotRequired}};
    auto *f = form->findChild<PasswordField *>(name);
    if (!f || !options.contains(value))
        return false;
    f->setPasswordOption(options.value(value));
    return true;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // NetworkManagerQt registers these only once something has asked it for its state.
    qDBusRegisterMetaType<NMStringMap>();
    qDBusRegisterMetaType<NMVariantMapMap>();
    const QStringList args = app.arguments();
    if (args.size() < 4 || (args[2] != QLatin1String("new") && args[2] != QLatin1String("edit")))
        return fail(QStringLiteral("usage: editor-form <service> new <name> | edit <uuid> [<object>=<value> ...]"));
    const QString service = args[1];
    const bool edit = args[2] == QLatin1String("edit");

    auto plugin = VpnUiPlugin::loadPluginForType(nullptr, service);
    if (!plugin)
        return fail(QStringLiteral("no editor for %1: %2").arg(service, plugin.errorString));

    NetworkManager::ConnectionSettings::Ptr settings;
    NetworkManager::Connection::Ptr connection;
    if (edit) {
        connection = NetworkManager::findConnectionByUuid(args[3]);
        if (!connection)
            return fail(QStringLiteral("no connection %1").arg(args[3]));
        settings = connection->settings();
        // As plasma-nm's editor: the secrets are asked from NM and loaded into the setting before the widget.
        QDBusPendingReply<NMVariantMapMap> reply = connection->secrets(QStringLiteral("vpn"));
        reply.waitForFinished();
        if (!reply.isError())
            settings->setting(NetworkManager::Setting::Vpn)->secretsFromMap(reply.value().value(QStringLiteral("vpn")));
    } else {
        settings.reset(new NetworkManager::ConnectionSettings(NetworkManager::ConnectionSettings::Vpn));
        settings->setId(args[3]);
        settings->setUuid(NetworkManager::ConnectionSettings::createNewUuid());
        settings->setAutoconnect(false);
    }
    auto vpn = settings->setting(NetworkManager::Setting::Vpn).staticCast<NetworkManager::VpnSetting>();
    vpn->setServiceType(service);
    SettingWidget *form = plugin.plugin->widget(vpn, nullptr);

    for (const QString &arg : args.mid(4)) {
        const qsizetype eq = arg.indexOf(QLatin1Char('='));
        const QString name = arg.left(eq), value = arg.mid(eq + 1);
        const bool ok = name.endsWith(QLatin1String(":option")) ? option(form, name.chopped(7), value) : set(form, name, value);
        if (eq < 0 || !ok)
            return fail(QStringLiteral("cannot set %1").arg(arg));
    }

    QJsonArray problems;
    for (const QLabel *label : form->findChildren<QLabel *>(QStringLiteral("problems")))
        if (!label->text().isEmpty())
            problems.append(label->text());
    QJsonObject out{{QStringLiteral("valid"), form->isValid()}, {QStringLiteral("problems"), problems},
                    {QStringLiteral("uuid"), settings->uuid()}};
    // The kill switch banner (6.2), when the form shows it.
    if (const auto *banner = form->findChild<KMessageWidget *>(); banner && banner->isVisibleTo(form))
        out.insert(QStringLiteral("banner"), banner->text());
    if (form->isValid()) {
        // ConnectionEditorBase::setting(): the widget's map goes through ConnectionSettings::fromMap.
        NMVariantMapMap map = settings->toMap();
        map.insert(QStringLiteral("vpn"), form->setting());
        NetworkManager::ConnectionSettings::Ptr saved(new NetworkManager::ConnectionSettings);
        saved->fromMap(map);
        QDBusPendingReply<> reply = edit ? connection->update(saved->toMap())
                                         : QDBusPendingReply<>(NetworkManager::addConnection(saved->toMap()));
        reply.waitForFinished();
        if (reply.isError())
            out.insert(QStringLiteral("error"), reply.error().message());
    }
    std::cout << QJsonDocument(out).toJson(QJsonDocument::Compact).constData() << std::endl;
    return out.contains(QStringLiteral("error")) ? 1 : 0;
}
