// SPDX-License-Identifier: MIT
#include "pnv-editor.h"

#include <KLocalizedString>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>

#include "killswitchbanner.h"
#include "pnv-profile.h"
#include "pnv-secrets.h"

QLabel *pnvBuddyLabel(const QString &text, QWidget *buddy)
{
    auto *label = new QLabel(text);
    label->setBuddy(buddy);
    return label;
}

PnvEditorWidget::PnvEditorWidget(const QString &service, const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent)
    : SettingWidget(setting, parent)
    , m_service(service)
{
    // plasma-nm's dialog does not grow with the form: the groups scroll instead of being squeezed.
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    scroll->setWidget(content);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    m_content = new QVBoxLayout(content);
    m_content->addWidget(new PnvKillSwitchBanner(content));
}

void PnvEditorWidget::addGroup(const QString &title, const QList<PnvBlock *> &blocks)
{
    auto *box = new QGroupBox(title);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    for (PnvBlock *block : blocks) {
        layout->addWidget(block);
        m_blocks.append(block);
        connect(block, &PnvBlock::changed, this, [this] {
            Q_EMIT validChanged(isValid());
        });
    }
    m_content->addWidget(box);
}

PasswordField *PnvEditorWidget::secretField(const QString &key, bool optional)
{
    auto *field = new PasswordField;
    field->setObjectName(key);
    field->setPasswordModeEnabled(true);
    field->setPasswordOptionsEnabled(true);
    field->setPasswordNotRequiredEnabled(optional);
    m_secrets.append({key, field});
    if (optional)
        m_optional.append(key);
    return field;
}

PnvAuthBlock *PnvEditorWidget::authBlock(const QString &label, const QString &key, PasswordField *field)
{
    auto *block = new PnvAuthBlock(m_service, label, field, key, [field] {
        return field->text();
    });
    connect(field, &PasswordField::textChanged, block, &PnvAuthBlock::secretChanged);
    return block;
}

PasswordField::PasswordOption PnvEditorWidget::secretOption(const QString &, const PasswordField *field) const
{
    return field->passwordOption();
}

void PnvEditorWidget::addImport(const QList<PnvImportWidget::ListFile> &lists)
{
    auto *box = new QGroupBox(i18nc("@title:group", "Import"));
    auto *layout = new QVBoxLayout(box);
    layout->addWidget(new PnvImportWidget(m_service, lists, [this](const NMStringMap &data, const NMStringMap &secrets) {
        return applyImport(data, secrets);
    }));
    // Under the kill switch banner, above the groups.
    m_content->insertWidget(1, box);
}

void PnvEditorWidget::setImportConfirmation(std::function<bool()> confirm)
{
    m_confirm = std::move(confirm);
}

// A server or a secret already entered.
bool PnvEditorWidget::filled() const
{
    NMStringMap data;
    for (PnvBlock *block : m_blocks)
        block->write(data);
    if (!data.value(QStringLiteral("server")).isEmpty())
        return true;
    for (const auto &[key, field] : m_secrets)
        if (!field->text().isEmpty())
            return true;
    return false;
}

bool PnvEditorWidget::applyImport(const NMStringMap &data, const NMStringMap &secrets)
{
    if (filled()) {
        const bool replace = m_confirm ? m_confirm()
                                       : QMessageBox::question(this, i18nc("@title:window", "Replace the Fields?"),
                                                               i18nc("@info", "The imported settings replace the ones in the form."))
                == QMessageBox::Yes;
        if (!replace)
            return false;
    }
    for (PnvBlock *block : std::as_const(m_blocks))
        block->load(data);
    // The storage stays the field's default; an optional secret the import has none of is not required.
    for (const auto &[key, field] : std::as_const(m_secrets)) {
        field->setText(secrets.value(key));
        if (secrets.value(key).isEmpty() && m_optional.contains(key))
            field->setPasswordOption(PasswordField::NotRequired);
    }
    Q_EMIT validChanged(isValid());
    slotWidgetChanged();
    return true;
}

void PnvEditorWidget::finish(const NetworkManager::VpnSetting::Ptr &setting)
{
    m_content->addStretch();
    if (setting)
        loadConfig(setting);
    watchChangedSetting();
}

void PnvEditorWidget::loadConfig(const NetworkManager::Setting::Ptr &setting)
{
    const NMStringMap data = setting.staticCast<NetworkManager::VpnSetting>()->data();
    for (PnvBlock *block : std::as_const(m_blocks))
        block->load(data);
    for (const auto &[key, field] : std::as_const(m_secrets))
        pnvLoadSecretOption({key, field}, data);
    loadSecrets(setting);
}

void PnvEditorWidget::loadSecrets(const NetworkManager::Setting::Ptr &setting)
{
    const NMStringMap secrets = setting.staticCast<NetworkManager::VpnSetting>()->secrets();
    for (const auto &[key, field] : std::as_const(m_secrets))
        if (secrets.contains(key))
            field->setText(secrets.value(key));
}

QVariantMap PnvEditorWidget::setting() const
{
    NetworkManager::VpnSetting vpn;
    vpn.setServiceType(m_service);
    NMStringMap data, secrets;
    data.insert(QStringLiteral("schema"), QString::number(PNV_SCHEMA_VERSION));
    for (PnvBlock *block : m_blocks)
        block->write(data);
    for (const auto &[key, field] : m_secrets)
        pnvWriteSecret({key, field}, secretOption(key, field), data, secrets);
    vpn.setData(data);
    vpn.setSecrets(secrets);
    // plasma-nm replaces the whole vpn setting with this one, so persistent is written on every save.
    vpn.setPersistent(true);
    return vpn.toMap();
}

bool PnvEditorWidget::isValid() const
{
    for (PnvBlock *block : m_blocks)
        if (!block->isValid())
            return false;
    return true;
}

PnvAuthWidget::PnvAuthWidget(const NetworkManager::VpnSetting::Ptr &setting, const QStringList &hints,
                             const QList<PnvPromptSecret> &secrets, QWidget *parent)
    : SettingWidget(setting, hints, parent)
{
    const NMStringMap data = setting->data();
    const NMStringMap stored = setting->secrets();
    auto *form = new QFormLayout(this);
    for (const PnvPromptSecret &secret : secrets) {
        if (!secret.onlyWith.isEmpty() && data.value(secret.onlyWith).isEmpty())
            continue;
        const NetworkManager::Setting::SecretFlags flags(data.value(secret.key + QLatin1String("-flags")).toInt());
        const bool needed = hints.contains(secret.key)
            || (!flags.testFlag(NetworkManager::Setting::NotRequired) && stored.value(secret.key).isEmpty());
        if (!needed)
            continue;
        auto *field = new PasswordField(this);
        field->setObjectName(secret.key);
        field->setPasswordModeEnabled(true);
        form->addRow(pnvBuddyLabel(secret.label, field), field);
        connect(field, &PasswordField::textChanged, this, &PnvAuthWidget::slotWidgetChanged);
        m_fields.append({secret.key, field});
    }
    watchChangedSetting();
}

QVariantMap PnvAuthWidget::setting() const
{
    NMStringMap secrets;
    for (const auto &[key, field] : m_fields)
        secrets.insert(key, field->text());
    NetworkManager::VpnSetting vpn;
    vpn.setSecrets(secrets);
    return vpn.toMap();
}

bool PnvAuthWidget::isValid() const
{
    for (const auto &[key, field] : m_fields)
        if (field->text().isEmpty())
            return false;
    return true;
}
