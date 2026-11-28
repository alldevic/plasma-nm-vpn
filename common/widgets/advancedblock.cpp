// SPDX-License-Identifier: MIT
#include "advancedblock.h"

#include "ui_advancedblock.h"
#include "util.h"

static const QString Mtu = QStringLiteral("mtu");
static const QString Extra = QStringLiteral("extra");

PnvAdvancedBlock::PnvAdvancedBlock(const QString &service, const QString &extraAllowlist, QWidget *parent)
    : PnvBlock(service, parent)
    , ui(std::make_unique<Ui::AdvancedBlock>())
    , m_extra(!extraAllowlist.isEmpty())
{
    ui->setupUi(this);
    if (m_extra)
        loadList(extraAllowlist, "extra/");
    ui->extraLabel->setVisible(m_extra);
    ui->extra->setVisible(m_extra);
    connect(ui->mtu, &QLineEdit::textChanged, this, &PnvAdvancedBlock::fieldChanged);
    connect(ui->extra, &QPlainTextEdit::textChanged, this, &PnvAdvancedBlock::fieldChanged);
    setProblemLabel(ui->problems);
}

PnvAdvancedBlock::~PnvAdvancedBlock() = default;

void PnvAdvancedBlock::load(const NMStringMap &data)
{
    ui->mtu->setText(data.value(Mtu));
    if (m_extra)
        ui->extra->setPlainText(data.value(Extra));
}

void PnvAdvancedBlock::write(NMStringMap &data) const
{
    pnvSet(data, Mtu, ui->mtu->text());
    if (m_extra)
        pnvSet(data, Extra, ui->extra->toPlainText());
}

QList<PnvBlock::Field> PnvAdvancedBlock::fields() const
{
    QList<Field> list{{Mtu, pnvLabel(ui->mtuLabel)}};
    if (m_extra)
        list.append({Extra, pnvLabel(ui->extraLabel)});
    return list;
}
