// SPDX-License-Identifier: MIT
#include "hysteria2widget.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include "advancedblock.h"
#include "authblock.h"
#include "passwordfield.h"
#include "pnv-profile.h"
#include "protectionblock.h"
#include "serverblock.h"
#include "tlsblock.h"

static const QString Service = QStringLiteral(PNV_SERVICE_HYSTERIA2);
static const QString Password = QStringLiteral("password");
static const QString ObfsPassword = QStringLiteral("obfs-password");
static const QString Obfs = QStringLiteral("obfs");


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
    form->addRow(pnvBuddyLabel(i18nc("@label:listbox", "Obfuscation:"), m_obfs), m_obfs);
    form->addRow(m_obfsPasswordLabel = pnvBuddyLabel(i18nc("@label:textbox", "Obfuscation password:"), m_obfsPassword),
                 m_obfsPassword);
    form->addRow(m_hopLabel = pnvBuddyLabel(i18nc("@label:textbox", "Port hopping interval:"), m_hopInterval), m_hopInterval);
    form->addRow(m_upLabel = pnvBuddyLabel(i18nc("@label:textbox", "Upload, Mbit/s:"), m_up), m_up);
    form->addRow(m_downLabel = pnvBuddyLabel(i18nc("@label:textbox", "Download, Mbit/s:"), m_down), m_down);
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
        QString text = l->text().remove(QLatin1Char('&'));
        return text.endsWith(QLatin1Char(':')) ? text.chopped(1) : text;
    };
    return {{Obfs, i18nc("@label", "Obfuscation")},
            {QStringLiteral("hop-interval"), name(m_hopLabel)},
            {QStringLiteral("up-mbps"), name(m_upLabel)},
            {QStringLiteral("down-mbps"), name(m_downLabel)}};
}

Hysteria2Widget::Hysteria2Widget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent,
                                 const QString &extraAllowlist)
    : PnvEditorWidget(Service, setting, parent)
{
    PasswordField *password = secretField(Password, false);
    PasswordField *obfsPassword = secretField(ObfsPassword, true);
    m_obfs = new Hysteria2ObfsBlock(obfsPassword);
    addGroup(i18nc("@title:group", "Server"), {new PnvServerBlock(Service, PnvServerBlock::PortList)});
    addGroup(i18nc("@title:group", "Authentication"), {authBlock(i18nc("@label:textbox", "Password:"), Password, password)});
    // No fingerprint over QUIC, no pin field (design.md, "Profile data schema").
    addGroup(i18nc("@title:group", "TLS"), {new PnvTlsBlock(Service)});
    addGroup(i18nc("@title:group", "Obfuscation, port hopping"), {m_obfs});
    addGroup(i18nc("@title:group", "Protection"), {new PnvProtectionBlock(Service)});
    addGroup(i18nc("@title:group", "Advanced"), {new PnvAdvancedBlock(Service, extraAllowlist)});
    addImport({{"extra/", extraAllowlist}});
    finish(setting);
}

// Without obfuscation its password is not needed, whatever the field says.
PasswordField::PasswordOption Hysteria2Widget::secretOption(const QString &key, const PasswordField *field) const
{
    return key == ObfsPassword && !m_obfs->salamander() ? PasswordField::NotRequired : field->passwordOption();
}

QList<PnvPromptSecret> hysteria2PromptSecrets()
{
    return {{Password, i18nc("@label:textbox", "Password:"), {}},
            {ObfsPassword, i18nc("@label:textbox", "Obfuscation password:"), Obfs}};
}
