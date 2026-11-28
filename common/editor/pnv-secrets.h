// SPDX-License-Identifier: MIT
// The editors' secret fields: plasma-nm's PasswordField and the <key>-flags in vpn.data (design.md, "Secrets").
// Header-only for the editors in the core packages; the shared blocks stay free of plasma-nm's API.
#pragma once

#include <NetworkManagerQt/GenericTypes>
#include <NetworkManagerQt/Setting>

#include "passwordfield.h"

struct PnvSecretField {
    QString key;
    PasswordField *field;
};

inline NetworkManager::Setting::SecretFlags pnvSecretFlags(PasswordField::PasswordOption option)
{
    switch (option) {
    case PasswordField::StoreForUser:
        return NetworkManager::Setting::AgentOwned;
    case PasswordField::StoreForAllUsers:
        return NetworkManager::Setting::None;
    case PasswordField::AlwaysAsk:
        return NetworkManager::Setting::NotSaved;
    case PasswordField::NotRequired:
        break;
    }
    return NetworkManager::Setting::NotRequired;
}

// Without <key>-flags the field keeps PasswordField's own default (StoreForUser with a keychain).
inline void pnvLoadSecretOption(const PnvSecretField &secret, const NMStringMap &data)
{
    const QString value = data.value(secret.key + QLatin1String("-flags"));
    bool ok = false;
    const NetworkManager::Setting::SecretFlags flags(value.toInt(&ok));
    if (!ok)
        return;
    if (flags.testFlag(NetworkManager::Setting::NotRequired))
        secret.field->setPasswordOption(PasswordField::NotRequired);
    else if (flags.testFlag(NetworkManager::Setting::NotSaved))
        secret.field->setPasswordOption(PasswordField::AlwaysAsk);
    else if (flags.testFlag(NetworkManager::Setting::AgentOwned))
        secret.field->setPasswordOption(PasswordField::StoreForUser);
    else
        secret.field->setPasswordOption(PasswordField::StoreForAllUsers);
}

// The flags go to vpn.data, a stored secret to vpn.secrets; one asked every time or not required is not written.
inline void pnvWriteSecret(const PnvSecretField &secret, PasswordField::PasswordOption option, NMStringMap &data,
                           NMStringMap &secrets)
{
    data.insert(secret.key + QLatin1String("-flags"), QString::number(int(pnvSecretFlags(option))));
    const QString text = secret.field->text();
    if ((option == PasswordField::StoreForUser || option == PasswordField::StoreForAllUsers) && !text.isEmpty())
        secrets.insert(secret.key, text);
}
