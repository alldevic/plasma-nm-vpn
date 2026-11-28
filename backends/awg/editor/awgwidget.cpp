// SPDX-License-Identifier: MIT
#include "awgwidget.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

#include "advancedblock.h"
#include "messages.h"
#include "pnv-profile.h"
#include "protectionblock.h"
#include "serverblock.h"

static const QString Service = QStringLiteral(PNV_SERVICE_AMNEZIAWG);
static const QString PrivateKey = QStringLiteral("private-key");
static const QString PresharedKey = QStringLiteral("preshared-key");
static const QString HeaderProtectionKey = QStringLiteral("awg-headerprotectionkey");

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
static T *named(const QString &name, QWidget *parent)
{
    auto *w = new T(parent);
    w->setObjectName(name);
    return w;
}

static QLabel *problemLabel(QFormLayout *form, QWidget *parent)
{
    auto *problems = named<QLabel>(QStringLiteral("problems"), parent);
    form->addRow(problems);
    return problems;
}

// The CIDR list through the 2.2 library, with this editor's limit; the message if it does not pass.
static QString cidrProblem(const char *key, const QString &value, guint max, const QString &label, GArray **out = nullptr)
{
    g_auto(PnvProblem) problem = {};
    GArray *list = nullptr;
    const QByteArray v = value.toUtf8();
    if (!pnv_parse_cidr4_list(key, v.constData(), max, &list, &problem))
        return pnvProblemMessage(problem, label);
    if (out)
        *out = list;
    else
        g_array_unref(list);
    return {};
}

AwgInterfaceBlock::AwgInterfaceBlock(QWidget *parent)
    : PnvBlock(Service, parent)
    , m_address(named<QLineEdit>(QStringLiteral("address"), this))
{
    m_address->setPlaceholderText(i18nc("@info:placeholder", "One IPv4 address with its prefix, for example 10.8.0.2/32"));
    auto *form = new QFormLayout(this);
    m_label = pnvBuddyLabel(i18nc("@label:textbox", "Address:"), m_address);
    form->addRow(m_label, m_address);
    connect(m_address, &QLineEdit::textChanged, this, &AwgInterfaceBlock::fieldChanged);
    setProblemLabel(problemLabel(form, this));
}

void AwgInterfaceBlock::load(const NMStringMap &data)
{
    m_address->setText(data.value(QStringLiteral("address")));
}

void AwgInterfaceBlock::write(NMStringMap &data) const
{
    set(data, QStringLiteral("address"), m_address->text());
}

QList<PnvBlock::Field> AwgInterfaceBlock::fields() const
{
    return {{QStringLiteral("address"), fieldName(m_label)}};
}

QStringList AwgInterfaceBlock::extraProblems() const
{
    if (m_address->text().isEmpty())
        return {};
    const QString problem = cidrProblem("address", m_address->text(), 1, fieldName(m_label));
    return problem.isEmpty() ? QStringList() : QStringList{problem};
}

AwgPeerBlock::AwgPeerBlock(QWidget *parent)
    : PnvBlock(Service, parent)
    , m_peerKey(named<QLineEdit>(QStringLiteral("peerPublicKey"), this))
    , m_allowedIps(named<QLineEdit>(QStringLiteral("allowedIps"), this))
    , m_keepalive(named<QLineEdit>(QStringLiteral("keepalive"), this))
{
    m_peerKey->setPlaceholderText(i18nc("@info:placeholder", "Base64"));
    m_allowedIps->setPlaceholderText(i18nc("@info:placeholder", "0.0.0.0/0 for all traffic, or IPv4 subnets"));
    m_keepalive->setPlaceholderText(i18nc("@info:placeholder", "Seconds; off when empty"));
    auto *form = new QFormLayout(this);
    for (const auto &[text, edit] : {std::pair{i18nc("@label:textbox", "Public key:"), m_peerKey},
                                     std::pair{i18nc("@label:textbox", "Allowed IPs:"), m_allowedIps},
                                     std::pair{i18nc("@label:textbox", "Keepalive:"), m_keepalive}}) {
        QLabel *label = pnvBuddyLabel(text, edit);
        form->addRow(label, edit);
        m_labels.append(label);
        connect(edit, &QLineEdit::textChanged, this, &AwgPeerBlock::fieldChanged);
    }
    setProblemLabel(problemLabel(form, this));
}

void AwgPeerBlock::load(const NMStringMap &data)
{
    m_peerKey->setText(data.value(QStringLiteral("peer-public-key")));
    m_allowedIps->setText(data.value(QStringLiteral("allowed-ips")));
    m_keepalive->setText(data.value(QStringLiteral("keepalive")));
}

QString AwgPeerBlock::normalizedAllowedIps(bool *defaultRoute) const
{
    GArray *list = nullptr;
    if (defaultRoute)
        *defaultRoute = false;
    if (!cidrProblem("allowed-ips", m_allowedIps->text(), 64, {}, &list).isEmpty() || !list)
        return m_allowedIps->text();
    QStringList entries;
    for (guint i = 0; i < list->len; i++) {
        const PnvCidr4 c = g_array_index(list, PnvCidr4, i);
        entries << QStringLiteral("%1.%2.%3.%4/%5")
                       .arg(c.address >> 24)
                       .arg((c.address >> 16) & 0xff)
                       .arg((c.address >> 8) & 0xff)
                       .arg(c.address & 0xff)
                       .arg(c.prefix);
        if (defaultRoute && c.prefix == 0)
            *defaultRoute = true;
    }
    g_array_unref(list);
    return entries.join(QStringLiteral(", "));
}

void AwgPeerBlock::write(NMStringMap &data) const
{
    set(data, QStringLiteral("peer-public-key"), m_peerKey->text());
    // 0.0.0.0/1 + 128.0.0.0/1 is saved as 0.0.0.0/0, as the importer and the daemon read it.
    set(data, QStringLiteral("allowed-ips"), normalizedAllowedIps());
    set(data, QStringLiteral("keepalive"), m_keepalive->text());
}

bool AwgPeerBlock::defaultRoute() const
{
    bool result = false;
    normalizedAllowedIps(&result);
    return result;
}

QList<PnvBlock::Field> AwgPeerBlock::fields() const
{
    return {{QStringLiteral("peer-public-key"), fieldName(m_labels[0])},
            {QStringLiteral("allowed-ips"), fieldName(m_labels[1])},
            {QStringLiteral("keepalive"), fieldName(m_labels[2])}};
}

// IPv6 in the tunnel is phase 2: the editor takes IPv4 only, where the schema skips IPv6 entries of a .conf.
QStringList AwgPeerBlock::extraProblems() const
{
    if (m_allowedIps->text().isEmpty())
        return {};
    const QString problem = cidrProblem("allowed-ips", m_allowedIps->text(), 64, fieldName(m_labels[1]));
    return problem.isEmpty() ? QStringList() : QStringList{problem};
}

AwgParamsBlock::AwgParamsBlock(const QString &allowlist, QWidget *parent)
    : PnvBlock(Service, parent)
{
    loadList(allowlist, "awg/");
    for (const QString &key : listEntries("awg/")) {
        const QByteArray entry = QByteArrayLiteral("awg/") + key.toUtf8();
        const char *const *fields = pnv_list_fields(list(), entry.constData());
        m_keys << key;
        m_names << (fields && fields[0] ? QString::fromUtf8(fields[0]) : key);
    }
    auto *layout = new QVBoxLayout(this);
    m_rows = new QVBoxLayout;
    layout->addLayout(m_rows);
    m_add = named<QPushButton>(QStringLiteral("addParam"), this);
    m_add->setText(i18nc("@action:button", "Add Parameter"));
    m_add->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    layout->addWidget(m_add, 0, Qt::AlignLeft);
    auto *problems = named<QLabel>(QStringLiteral("problems"), this);
    layout->addWidget(problems);
    connect(m_add, &QPushButton::clicked, this, [this] {
        addRow();
        fieldChanged();
    });
    setProblemLabel(problems);
    refresh();
}

QStringList AwgParamsBlock::usedKeys(const QComboBox *except) const
{
    QStringList used;
    for (const Row &row : m_rowList)
        if (row.key != except)
            used << row.key->currentData().toString();
    return used;
}

void AwgParamsBlock::addRow(const QString &key, const QString &value)
{
    const int n = m_serial++;
    auto *widget = new QWidget(this);
    auto *h = new QHBoxLayout(widget);
    h->setContentsMargins(0, 0, 0, 0);
    auto *box = named<QComboBox>(QStringLiteral("paramKey%1").arg(n), widget);
    auto *edit = named<QLineEdit>(QStringLiteral("paramValue%1").arg(n), widget);
    auto *remove = named<QToolButton>(QStringLiteral("removeParam%1").arg(n), widget);
    remove->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
    remove->setToolTip(i18nc("@info:tooltip", "Remove this parameter"));
    // AmneziaWG's own names, not translated.
    for (int i = 0; i < m_keys.size(); i++)
        box->addItem(m_names[i], m_keys[i]);
    const QStringList used = usedKeys();
    QString chosen = key;
    if (chosen.isEmpty())
        for (const QString &k : std::as_const(m_keys))
            if (!used.contains(k)) {
                chosen = k;
                break;
            }
    int index = box->findData(chosen);
    if (index < 0) {
        // A key the allowlist does not have stays, for the validator to name.
        box->addItem(chosen, chosen);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
    edit->setText(value);
    h->addWidget(box);
    h->addWidget(edit, 1);
    h->addWidget(remove);
    m_rows->addWidget(widget);
    m_rowList.append({widget, box, edit});
    connect(box, &QComboBox::currentIndexChanged, this, [this] {
        refresh();
        fieldChanged();
    });
    connect(edit, &QLineEdit::textChanged, this, &AwgParamsBlock::fieldChanged);
    connect(remove, &QToolButton::clicked, this, [this, widget] {
        removeRow(widget);
        fieldChanged();
    });
    refresh();
}

void AwgParamsBlock::removeRow(QWidget *row)
{
    for (int i = 0; i < m_rowList.size(); i++)
        if (m_rowList[i].widget == row) {
            m_rowList.removeAt(i);
            break;
        }
    row->deleteLater();
    refresh();
}

// A key used in one row cannot be chosen in another; Add stops when every key is used.
void AwgParamsBlock::refresh()
{
    for (const Row &row : std::as_const(m_rowList)) {
        const QStringList others = usedKeys(row.key);
        auto *model = qobject_cast<QStandardItemModel *>(row.key->model());
        for (int i = 0; model && i < row.key->count(); i++)
            model->item(i)->setEnabled(!others.contains(row.key->itemData(i).toString()));
    }
    const QStringList used = usedKeys();
    bool free = false;
    for (const QString &k : std::as_const(m_keys))
        free = free || !used.contains(k);
    m_add->setEnabled(free);
}

void AwgParamsBlock::load(const NMStringMap &data)
{
    while (!m_rowList.isEmpty())
        removeRow(m_rowList.first().widget);
    for (auto it = data.cbegin(); it != data.cend(); ++it)
        if (it.key().startsWith(QLatin1String("awg-")) && !it.key().endsWith(QLatin1String("-flags")))
            addRow(it.key(), it.value());
    // The rows are made without a change signal.
    fieldChanged();
}

void AwgParamsBlock::write(NMStringMap &data) const
{
    for (auto it = data.begin(); it != data.end();)
        it = it.key().startsWith(QLatin1String("awg-")) && !it.key().endsWith(QLatin1String("-flags")) ? data.erase(it)
                                                                                                       : std::next(it);
    for (const Row &row : m_rowList)
        set(data, row.key->currentData().toString(), row.value->text());
}

QList<PnvBlock::Field> AwgParamsBlock::fields() const
{
    QList<Field> list;
    for (const Row &row : m_rowList)
        list.append({row.key->currentData().toString(), row.key->currentText()});
    return list;
}

AwgWidget::AwgWidget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent, const QString &dataDir)
    : PnvEditorWidget(Service, setting, parent)
{
    PasswordField *privateKey = secretField(PrivateKey, false);
    PasswordField *presharedKey = secretField(PresharedKey, true);
    PasswordField *headerKey = secretField(HeaderProtectionKey, true);
    m_peer = new AwgPeerBlock;
    m_protection = new PnvProtectionBlock(Service);
    addGroup(i18nc("@title:group", "Interface"),
             {authBlock(i18nc("@label:textbox", "Private key:"), PrivateKey, privateKey), new AwgInterfaceBlock});
    addGroup(i18nc("@title:group", "Peer"),
             {new PnvServerBlock(Service, PnvServerBlock::SinglePort), m_peer,
              authBlock(i18nc("@label:textbox", "Preshared key:"), PresharedKey, presharedKey)});
    addGroup(i18nc("@title:group", "AmneziaWG parameters"),
             {authBlock(i18nc("@label:textbox", "Header protection key:"), HeaderProtectionKey, headerKey),
              new AwgParamsBlock(dataDir + QStringLiteral("/amneziawg-params.list"))});
    addGroup(i18nc("@title:group", "Protection"), {m_protection});
    addGroup(i18nc("@title:group", "Advanced"), {new PnvAdvancedBlock(Service)});
    addImport({{"awg/", dataDir + QStringLiteral("/amneziawg-params.list")}});
    connect(m_peer, &PnvBlock::changed, this, &AwgWidget::updateKillSwitch);
    finish(setting);
    updateKillSwitch();
}

void AwgWidget::updateKillSwitch()
{
    m_protection->setKillSwitchDisabled(
        m_peer->defaultRoute() ? QString()
                               : i18nc("@info", "Allowed IPs do not include 0.0.0.0/0, so not all traffic goes through the VPN"));
}

// An optional key left empty is not required, unless the user chose to be asked for it.
PasswordField::PasswordOption AwgWidget::secretOption(const QString &key, const PasswordField *field) const
{
    const PasswordField::PasswordOption option = field->passwordOption();
    if (key != PrivateKey && field->text().isEmpty()
        && (option == PasswordField::StoreForUser || option == PasswordField::StoreForAllUsers))
        return PasswordField::NotRequired;
    return option;
}

QList<PnvPromptSecret> awgPromptSecrets()
{
    return {{PrivateKey, i18nc("@label:textbox", "Private key:"), {}},
            {PresharedKey, i18nc("@label:textbox", "Preshared key:"), {}},
            {HeaderProtectionKey, i18nc("@label:textbox", "Header protection key:"), {}}};
}
