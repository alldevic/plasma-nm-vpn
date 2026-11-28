// SPDX-License-Identifier: MIT
#include "hysteria2widget.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

#include "advancedblock.h"
#include "authblock.h"
#include "killswitchbanner.h"
#include "passwordfield.h"
#include "pnv-profile.h"
#include "pnv-secrets.h"
#include "protectionblock.h"
#include "serverblock.h"
#include "tlsblock.h"

static const QString Service = QStringLiteral(PNV_SERVICE_HYSTERIA2);
static const QString Password = QStringLiteral("password");
static const QString ObfsPassword = QStringLiteral("obfs-password");
static const QString Obfs = QStringLiteral("obfs");

static PasswordField *secretField(const char *name, bool optional, QWidget *parent)
{
    auto *field = new PasswordField(parent);
    field->setObjectName(QLatin1String(name));
    field->setPasswordModeEnabled(true);
    field->setPasswordOptionsEnabled(true);
    field->setPasswordNotRequiredEnabled(optional);
    return field;
}

static QLabel *label(const QString &text, QWidget *buddy)
{
    auto *l = new QLabel(text);
    l->setBuddy(buddy);
    return l;
}

Hysteria2ObfsBlock::Hysteria2ObfsBlock(PasswordField *obfsPassword, QWidget *parent)
    : PnvBlock(Service, parent)
    , m_obfs(new QComboBox(this))
    , m_obfsPassword(obfsPassword)
    , m_hopInterval(new QLineEdit(this))
    , m_up(new QLineEdit(this))
    , m_down(new QLineEdit(this))
{
    m_obfs->setObjectName(QStringLiteral("obfs"));
    m_obfs->addItem(i18nc("@item:inlistbox no obfuscation", "None"), QString());
    m_obfs->addItem(QStringLiteral("Salamander"), QStringLiteral("salamander"));
    m_hopInterval->setObjectName(QStringLiteral("hopInterval"));
    m_hopInterval->setPlaceholderText(i18nc("@info:placeholder", "Seconds; the core's default when empty"));
    m_up->setObjectName(QStringLiteral("upMbps"));
    m_down->setObjectName(QStringLiteral("downMbps"));
    m_up->setPlaceholderText(i18nc("@info:placeholder", "Not set"));
    m_down->setPlaceholderText(i18nc("@info:placeholder", "Not set"));

    auto *form = new QFormLayout(this);
    form->addRow(label(i18nc("@label:listbox", "Obfuscation:"), m_obfs), m_obfs);
    form->addRow(m_obfsPasswordLabel = label(i18nc("@label:textbox", "Obfuscation password:"), m_obfsPassword),
                 m_obfsPassword);
    form->addRow(m_hopLabel = label(i18nc("@label:textbox", "Port hopping interval:"), m_hopInterval), m_hopInterval);
    form->addRow(m_upLabel = label(i18nc("@label:textbox", "Upload, Mbit/s:"), m_up), m_up);
    form->addRow(m_downLabel = label(i18nc("@label:textbox", "Download, Mbit/s:"), m_down), m_down);
    auto *problems = new QLabel(this);
    problems->setObjectName(QStringLiteral("problems"));
    form->addRow(problems);

    auto updatePassword = [this] {
        m_obfsPassword->setEnabled(salamander());
        m_obfsPasswordLabel->setEnabled(salamander());
    };
    connect(m_obfs, &QComboBox::currentIndexChanged, this, [this, updatePassword] {
        updatePassword();
        fieldChanged();
    });
    for (QLineEdit *edit : {m_hopInterval, m_up, m_down})
        connect(edit, &QLineEdit::textChanged, this, &Hysteria2ObfsBlock::fieldChanged);
    updatePassword();
    setProblemLabel(problems);
}

bool Hysteria2ObfsBlock::salamander() const
{
    return !m_obfs->currentData().toString().isEmpty();
}

void Hysteria2ObfsBlock::load(const NMStringMap &data)
{
    // An unknown value stays selected, for the validator to name.
    const QString obfs = data.value(Obfs);
    int index = m_obfs->findData(obfs);
    if (index < 0) {
        m_obfs->addItem(obfs, obfs);
        index = m_obfs->count() - 1;
    }
    m_obfs->setCurrentIndex(index);
    m_hopInterval->setText(data.value(QStringLiteral("hop-interval")));
    m_up->setText(data.value(QStringLiteral("up-mbps")));
    m_down->setText(data.value(QStringLiteral("down-mbps")));
}

static void set(NMStringMap &data, const QString &key, const QString &value)
{
    if (value.isEmpty())
        data.remove(key);
    else
        data.insert(key, value);
}

void Hysteria2ObfsBlock::write(NMStringMap &data) const
{
    set(data, Obfs, m_obfs->currentData().toString());
    set(data, QStringLiteral("hop-interval"), m_hopInterval->text());
    set(data, QStringLiteral("up-mbps"), m_up->text());
    set(data, QStringLiteral("down-mbps"), m_down->text());
}

QList<PnvBlock::Field> Hysteria2ObfsBlock::fields() const
{
    auto name = [](const QLabel *l) {
        QString text = l->text();
        return text.endsWith(QLatin1Char(':')) ? text.chopped(1) : text;
    };
    return {{Obfs, i18nc("@label", "Obfuscation")},
            {QStringLiteral("hop-interval"), name(m_hopLabel)},
            {QStringLiteral("up-mbps"), name(m_upLabel)},
            {QStringLiteral("down-mbps"), name(m_downLabel)}};
}

static QGroupBox *group(const QString &title, QWidget *content)
{
    auto *box = new QGroupBox(title);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
    return box;
}

Hysteria2Widget::Hysteria2Widget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent,
                                 const QString &extraAllowlist)
    : SettingWidget(setting, parent)
    , m_password(secretField("password", false, nullptr))
    , m_obfsPassword(secretField("obfsPassword", true, nullptr))
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
    auto *layout = new QVBoxLayout(content);
    auto *server = new PnvServerBlock(Service, PnvServerBlock::PortList);
    auto *auth = new PnvAuthBlock(Service, i18nc("@label:textbox", "Password:"), m_password);
    // No fingerprint over QUIC, no pin field (design.md, "Profile data schema").
    auto *tls = new PnvTlsBlock(Service);
    m_obfs = new Hysteria2ObfsBlock(m_obfsPassword);
    auto *protection = new PnvProtectionBlock(Service);
    auto *advanced = new PnvAdvancedBlock(Service, extraAllowlist);
    m_blocks = {server, auth, tls, m_obfs, protection, advanced};

    layout->addWidget(new PnvKillSwitchBanner(content));
    layout->addWidget(group(i18nc("@title:group", "Server"), server));
    layout->addWidget(group(i18nc("@title:group", "Authentication"), auth));
    layout->addWidget(group(i18nc("@title:group", "TLS"), tls));
    layout->addWidget(group(i18nc("@title:group", "Obfuscation, port hopping"), m_obfs));
    layout->addWidget(group(i18nc("@title:group", "Protection"), protection));
    layout->addWidget(group(i18nc("@title:group", "Advanced"), advanced));
    layout->addStretch();

    for (PnvBlock *block : std::as_const(m_blocks))
        connect(block, &PnvBlock::changed, this, [this] {
            Q_EMIT validChanged(isValid());
        });

    if (setting)
        loadConfig(setting);
    watchChangedSetting();
}

void Hysteria2Widget::loadConfig(const NetworkManager::Setting::Ptr &setting)
{
    const auto vpn = setting.staticCast<NetworkManager::VpnSetting>();
    const NMStringMap data = vpn->data();
    for (PnvBlock *block : std::as_const(m_blocks))
        block->load(data);
    pnvLoadSecretOption({Password, m_password}, data);
    pnvLoadSecretOption({ObfsPassword, m_obfsPassword}, data);
    loadSecrets(setting);
}

void Hysteria2Widget::loadSecrets(const NetworkManager::Setting::Ptr &setting)
{
    const auto vpn = setting.staticCast<NetworkManager::VpnSetting>();
    const NMStringMap secrets = vpn->secrets();
    if (secrets.contains(Password))
        m_password->setText(secrets.value(Password));
    if (secrets.contains(ObfsPassword))
        m_obfsPassword->setText(secrets.value(ObfsPassword));
}

QVariantMap Hysteria2Widget::setting() const
{
    NetworkManager::VpnSetting vpn;
    vpn.setServiceType(Service);
    NMStringMap data, secrets;
    data.insert(QStringLiteral("schema"), QString::number(PNV_SCHEMA_VERSION));
    for (PnvBlock *block : m_blocks)
        block->write(data);
    pnvWriteSecret({Password, m_password}, m_password->passwordOption(), data, secrets);
    // Without obfuscation the obfuscation password is not needed, whatever the field says.
    pnvWriteSecret({ObfsPassword, m_obfsPassword},
                   m_obfs->salamander() ? m_obfsPassword->passwordOption() : PasswordField::NotRequired, data, secrets);
    vpn.setData(data);
    vpn.setSecrets(secrets);
    // plasma-nm replaces the whole vpn setting with this one, so persistent is written on every save.
    vpn.setPersistent(true);
    return vpn.toMap();
}

bool Hysteria2Widget::isValid() const
{
    for (PnvBlock *block : m_blocks)
        if (!block->isValid())
            return false;
    return true;
}

Hysteria2AuthWidget::Hysteria2AuthWidget(const NetworkManager::VpnSetting::Ptr &setting, const QStringList &hints,
                                         QWidget *parent)
    : SettingWidget(setting, hints, parent)
{
    const NMStringMap data = setting->data();
    const NMStringMap stored = setting->secrets();
    auto needed = [&](const QString &key) {
        if (hints.contains(key))
            return true;
        bool ok = false;
        const NetworkManager::Setting::SecretFlags flags(data.value(key + QLatin1String("-flags")).toInt(&ok));
        return !flags.testFlag(NetworkManager::Setting::NotRequired) && stored.value(key).isEmpty();
    };
    auto *form = new QFormLayout(this);
    const QList<QPair<QString, QString>> secrets{{Password, i18nc("@label:textbox", "Password:")},
                                                 {ObfsPassword, i18nc("@label:textbox", "Obfuscation password:")}};
    for (const auto &[key, text] : secrets) {
        if (key == ObfsPassword && data.value(Obfs).isEmpty())
            continue;
        if (!needed(key))
            continue;
        auto *field = new PasswordField(this);
        field->setObjectName(key);
        field->setPasswordModeEnabled(true);
        form->addRow(label(text, field), field);
        connect(field, &PasswordField::textChanged, this, &Hysteria2AuthWidget::slotWidgetChanged);
        m_fields.append({key, field});
    }
    watchChangedSetting();
}

QVariantMap Hysteria2AuthWidget::setting() const
{
    NMStringMap secrets;
    for (const auto &[key, field] : m_fields)
        secrets.insert(key, field->text());
    NetworkManager::VpnSetting vpn;
    vpn.setSecrets(secrets);
    return vpn.toMap();
}

bool Hysteria2AuthWidget::isValid() const
{
    for (const auto &[key, field] : m_fields)
        if (field->text().isEmpty())
            return false;
    return true;
}
