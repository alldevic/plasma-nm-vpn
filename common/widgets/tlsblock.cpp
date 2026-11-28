// SPDX-License-Identifier: MIT
#include "tlsblock.h"

#include <KLocalizedString>

#include "ui_tlsblock.h"
#include "util.h"

static const QString Sni = QStringLiteral("tls-sni");
static const QString Alpn = QStringLiteral("tls-alpn");
static const QString Insecure = QStringLiteral("tls-insecure");
static const QString Fingerprint = QStringLiteral("tls-fingerprint");
static const QString Pin = QStringLiteral("tls-pin-sha256");

PnvTlsBlock::PnvTlsBlock(const QString &service, const QString &fingerprintList, bool pin, QWidget *parent)
    : PnvBlock(service, parent)
    , ui(std::make_unique<Ui::TlsBlock>())
    , m_fingerprint(!fingerprintList.isEmpty())
    , m_pin(pin)
{
    ui->setupUi(this);
    if (m_fingerprint) {
        loadList(fingerprintList, "tls-fingerprint/");
        ui->fingerprint->addItem(i18nc("@item:inlistbox the core's own choice", "Default"), QString());
        for (const QString &entry : listEntries("tls-fingerprint/"))
            ui->fingerprint->addItem(entry, entry);
    }
    ui->fingerprintLabel->setVisible(m_fingerprint);
    ui->fingerprint->setVisible(m_fingerprint);
    ui->pinLabel->setVisible(m_pin);
    ui->pin->setVisible(m_pin);
    connect(ui->sni, &QLineEdit::textChanged, this, &PnvTlsBlock::fieldChanged);
    connect(ui->alpn, &QLineEdit::textChanged, this, &PnvTlsBlock::fieldChanged);
    connect(ui->pin, &QLineEdit::textChanged, this, &PnvTlsBlock::fieldChanged);
    connect(ui->insecure, &QCheckBox::toggled, this, &PnvTlsBlock::fieldChanged);
    connect(ui->fingerprint, &QComboBox::currentIndexChanged, this, &PnvTlsBlock::fieldChanged);
    setProblemLabel(ui->problems);
}

PnvTlsBlock::~PnvTlsBlock() = default;

void PnvTlsBlock::load(const NMStringMap &data)
{
    ui->sni->setText(data.value(Sni));
    ui->alpn->setText(data.value(Alpn));
    ui->insecure->setChecked(data.value(Insecure) == QLatin1String("yes"));
    if (m_pin)
        ui->pin->setText(data.value(Pin));
    if (m_fingerprint) {
        // A value the list does not have stays, so that the validator names it rather than the form dropping it.
        const QString value = data.value(Fingerprint);
        int index = ui->fingerprint->findData(value);
        if (index < 0) {
            ui->fingerprint->addItem(value, value);
            index = ui->fingerprint->count() - 1;
        }
        ui->fingerprint->setCurrentIndex(index);
    }
}

void PnvTlsBlock::write(NMStringMap &data) const
{
    pnvSet(data, Sni, ui->sni->text());
    pnvSet(data, Alpn, ui->alpn->text());
    pnvSetBool(data, Insecure, ui->insecure->isChecked());
    if (m_pin)
        pnvSet(data, Pin, ui->pin->text());
    if (m_fingerprint)
        pnvSet(data, Fingerprint, ui->fingerprint->currentData().toString());
}

QList<PnvBlock::Field> PnvTlsBlock::fields() const
{
    QList<Field> list{{Sni, pnvLabel(ui->sniLabel)}, {Alpn, pnvLabel(ui->alpnLabel)},
                      {Insecure, ui->insecure->text().remove(QLatin1Char('&'))}};
    if (m_pin)
        list.append({Pin, pnvLabel(ui->pinLabel)});
    if (m_fingerprint)
        list.append({Fingerprint, pnvLabel(ui->fingerprintLabel)});
    return list;
}
