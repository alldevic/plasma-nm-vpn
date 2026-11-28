// SPDX-License-Identifier: MIT
#include "vlesswidget.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>

#include "advancedblock.h"
#include "pnv-profile.h"
#include "protectionblock.h"
#include "serverblock.h"

static const QString Service = QStringLiteral(PNV_SERVICE_VLESS);
static const QString Uuid = QStringLiteral("uuid");

static void set(NMStringMap &data, const QString &key, const QString &value)
{
    if (value.isEmpty())
        data.remove(key);
    else
        data.insert(key, value);
}

static QString fieldName(const QLabel *label)
{
    // KDE inserts accelerators into the labels at run time.
    const QString text = label->text().remove(QLatin1Char('&'));
    return text.endsWith(QLatin1Char(':')) ? text.chopped(1) : text;
}

template<typename T>
static T *named(const char *name, QWidget *parent)
{
    auto *w = new T(parent);
    w->setObjectName(QLatin1String(name));
    return w;
}

// A form row whose label also names the field in messages; the problems label goes last.
static QLabel *row(QFormLayout *form, const QString &text, QWidget *field, QList<QLabel *> &labels)
{
    QLabel *label = pnvBuddyLabel(text, field);
    form->addRow(label, field);
    labels.append(label);
    return label;
}

static QLabel *problemLabel(QFormLayout *form, QWidget *parent)
{
    auto *problems = named<QLabel>("problems", parent);
    form->addRow(problems);
    return problems;
}

void VlessChoice::fill(QComboBox *box, const QStringList &entries)
{
    // Values of the core, shown as they are.
    for (const QString &entry : entries)
        box->addItem(entry, entry);
}

void VlessChoice::select(QComboBox *box, const QString &value)
{
    if (value.isEmpty()) {
        box->setCurrentIndex(box->count() > 0 ? 0 : -1);
        return;
    }
    int index = box->findData(value);
    if (index < 0) {
        box->addItem(value, value);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
}

VlessEncryptionBlock::VlessEncryptionBlock(QWidget *parent)
    : PnvBlock(Service, parent)
    , m_encryption(named<QLineEdit>("encryption", this))
{
    m_encryption->setPlaceholderText(i18nc("@info:placeholder", "none, or a VLESS Encryption (ML-KEM) key"));
    m_encryption->setText(QStringLiteral("none"));
    auto *form = new QFormLayout(this);
    QList<QLabel *> labels;
    m_label = row(form, i18nc("@label:textbox", "Encryption:"), m_encryption, labels);
    connect(m_encryption, &QLineEdit::textChanged, this, &VlessEncryptionBlock::fieldChanged);
    setProblemLabel(problemLabel(form, this));
}

void VlessEncryptionBlock::load(const NMStringMap &data)
{
    const QString value = data.value(QStringLiteral("encryption"));
    m_encryption->setText(value.isEmpty() ? QStringLiteral("none") : value);
}

void VlessEncryptionBlock::write(NMStringMap &data) const
{
    set(data, QStringLiteral("encryption"), m_encryption->text());
}

QList<PnvBlock::Field> VlessEncryptionBlock::fields() const
{
    return {{QStringLiteral("encryption"), fieldName(m_label)}};
}

VlessRealityBlock::VlessRealityBlock(const QString &fingerprintList, QWidget *parent)
    : PnvBlock(Service, parent)
    , m_sni(named<QLineEdit>("sni", this))
    , m_pbk(named<QLineEdit>("pbk", this))
    , m_sid(named<QLineEdit>("sid", this))
    , m_spx(named<QLineEdit>("spx", this))
    , m_fingerprint(named<QComboBox>("fingerprint", this))
{
    loadList(fingerprintList, "tls-fingerprint/");
    VlessChoice::fill(m_fingerprint, listEntries("tls-fingerprint/"));
    m_sni->setPlaceholderText(i18nc("@info:placeholder", "The site the server borrows its TLS from"));
    m_pbk->setPlaceholderText(i18nc("@info:placeholder", "The server's public key, base64url"));
    m_sid->setPlaceholderText(i18nc("@info:placeholder", "Hex, up to 16 digits; may be empty"));
    m_spx->setPlaceholderText(QStringLiteral("/"));
    auto *form = new QFormLayout(this);
    row(form, i18nc("@label:textbox", "Server name (SNI):"), m_sni, m_labels);
    row(form, i18nc("@label:textbox", "Public key:"), m_pbk, m_labels);
    row(form, i18nc("@label:textbox", "Short ID:"), m_sid, m_labels);
    row(form, i18nc("@label:textbox", "Spider path:"), m_spx, m_labels);
    row(form, i18nc("@label:listbox", "Fingerprint:"), m_fingerprint, m_labels);
    for (QLineEdit *edit : {m_sni, m_pbk, m_sid, m_spx})
        connect(edit, &QLineEdit::textChanged, this, &VlessRealityBlock::fieldChanged);
    connect(m_fingerprint, &QComboBox::currentIndexChanged, this, &VlessRealityBlock::fieldChanged);
    setProblemLabel(problemLabel(form, this));
}

void VlessRealityBlock::load(const NMStringMap &data)
{
    m_sni->setText(data.value(QStringLiteral("tls-sni")));
    m_pbk->setText(data.value(QStringLiteral("reality-pbk")));
    m_sid->setText(data.value(QStringLiteral("reality-sid")));
    m_spx->setText(data.value(QStringLiteral("reality-spx")));
    VlessChoice::select(m_fingerprint, data.value(QStringLiteral("tls-fingerprint")));
}

void VlessRealityBlock::write(NMStringMap &data) const
{
    set(data, QStringLiteral("tls-sni"), m_sni->text());
    set(data, QStringLiteral("reality-pbk"), m_pbk->text());
    set(data, QStringLiteral("reality-sid"), m_sid->text());
    set(data, QStringLiteral("reality-spx"), m_spx->text());
    set(data, QStringLiteral("tls-fingerprint"), m_fingerprint->currentData().toString());
}

QList<PnvBlock::Field> VlessRealityBlock::fields() const
{
    const QStringList keys{QStringLiteral("tls-sni"), QStringLiteral("reality-pbk"), QStringLiteral("reality-sid"),
                           QStringLiteral("reality-spx"), QStringLiteral("tls-fingerprint")};
    QList<Field> list;
    for (int i = 0; i < keys.size(); i++)
        list.append({keys[i], fieldName(m_labels[i])});
    return list;
}

VlessXhttpBlock::VlessXhttpBlock(const QString &modeList, const QString &extraAllowlist, QWidget *parent)
    : PnvBlock(Service, parent)
    , m_mode(named<QComboBox>("mode", this))
    , m_path(named<QLineEdit>("path", this))
    , m_host(named<QLineEdit>("host", this))
    , m_extra(named<QPlainTextEdit>("xhttpExtra", this))
{
    loadList(modeList, "xhttp-mode/");
    loadList(extraAllowlist, "xhttp-extra/");
    VlessChoice::fill(m_mode, listEntries("xhttp-mode/"));
    m_path->setPlaceholderText(QStringLiteral("/"));
    m_host->setPlaceholderText(i18nc("@info:placeholder", "The server address"));
    m_extra->setPlaceholderText(i18nc("@info:placeholder", "XHTTP settings, for example {\"xPaddingBytes\": \"100-1000\"}"));
    auto *form = new QFormLayout(this);
    row(form, i18nc("@label:listbox", "Mode:"), m_mode, m_labels);
    row(form, i18nc("@label:textbox", "Path:"), m_path, m_labels);
    row(form, i18nc("@label:textbox", "Host:"), m_host, m_labels);
    row(form, i18nc("@label:textbox", "Additional XHTTP parameters (JSON):"), m_extra, m_labels);
    connect(m_mode, &QComboBox::currentIndexChanged, this, &VlessXhttpBlock::fieldChanged);
    for (QLineEdit *edit : {m_path, m_host})
        connect(edit, &QLineEdit::textChanged, this, &VlessXhttpBlock::fieldChanged);
    connect(m_extra, &QPlainTextEdit::textChanged, this, &VlessXhttpBlock::fieldChanged);
    setProblemLabel(problemLabel(form, this));
}

void VlessXhttpBlock::load(const NMStringMap &data)
{
    VlessChoice::select(m_mode, data.value(QStringLiteral("xhttp-mode")));
    m_path->setText(data.value(QStringLiteral("xhttp-path")));
    m_host->setText(data.value(QStringLiteral("xhttp-host")));
    m_extra->setPlainText(data.value(QStringLiteral("xhttp-extra")));
}

void VlessXhttpBlock::write(NMStringMap &data) const
{
    set(data, QStringLiteral("xhttp-mode"), m_mode->currentData().toString());
    set(data, QStringLiteral("xhttp-path"), m_path->text());
    set(data, QStringLiteral("xhttp-host"), m_host->text());
    set(data, QStringLiteral("xhttp-extra"), m_extra->toPlainText());
}

QList<PnvBlock::Field> VlessXhttpBlock::fields() const
{
    const QStringList keys{QStringLiteral("xhttp-mode"), QStringLiteral("xhttp-path"), QStringLiteral("xhttp-host"),
                           QStringLiteral("xhttp-extra")};
    QList<Field> list;
    for (int i = 0; i < keys.size(); i++)
        list.append({keys[i], fieldName(m_labels[i])});
    return list;
}

VlessWidget::VlessWidget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent, const QString &dataDir)
    : PnvEditorWidget(Service, setting, parent)
{
    PasswordField *uuid = secretField(Uuid, false);
    addGroup(i18nc("@title:group", "Server"), {new PnvServerBlock(Service, PnvServerBlock::SinglePort)});
    addGroup(i18nc("@title:group", "Authentication"),
             {authBlock(i18nc("@label:textbox", "UUID:"), Uuid, uuid), new VlessEncryptionBlock});
    addGroup(i18nc("@title:group", "Reality"), {new VlessRealityBlock(dataDir + QStringLiteral("/tls-fingerprint.list"))});
    addGroup(i18nc("@title:group", "XHTTP transport"),
             {new VlessXhttpBlock(dataDir + QStringLiteral("/xhttp-mode.list"), dataDir + QStringLiteral("/xhttp-extra.list"))});
    addGroup(i18nc("@title:group", "Protection"), {new PnvProtectionBlock(Service)});
    addGroup(i18nc("@title:group", "Advanced"), {new PnvAdvancedBlock(Service, dataDir + QStringLiteral("/vless-extra.list"))});
    finish(setting);
}

QList<PnvPromptSecret> vlessPromptSecrets()
{
    return {{Uuid, i18nc("@label:textbox", "UUID:"), {}}};
}
