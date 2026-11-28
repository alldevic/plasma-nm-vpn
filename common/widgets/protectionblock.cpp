// SPDX-License-Identifier: MIT
#include "protectionblock.h"

#include "ui_protectionblock.h"
#include "util.h"

static const QString KillSwitch = QStringLiteral("kill-switch");
static const QString AllowLan = QStringLiteral("allow-lan");
static const QString Dns = QStringLiteral("dns");

PnvProtectionBlock::PnvProtectionBlock(const QString &service, QWidget *parent)
    : PnvBlock(service, parent)
    , ui(std::make_unique<Ui::ProtectionBlock>())
{
    ui->setupUi(this);
    ui->killSwitchReason->setTextFormat(Qt::PlainText);
    ui->killSwitchReason->hide();
    ui->allowLan->setChecked(true);
    connect(ui->killSwitch, &QCheckBox::toggled, this, [this] {
        updateAllowLan();
        fieldChanged();
    });
    connect(ui->allowLan, &QCheckBox::toggled, this, &PnvProtectionBlock::fieldChanged);
    connect(ui->dns, &QLineEdit::textChanged, this, &PnvProtectionBlock::fieldChanged);
    updateAllowLan();
    setProblemLabel(ui->problems);
}

PnvProtectionBlock::~PnvProtectionBlock() = default;

void PnvProtectionBlock::load(const NMStringMap &data)
{
    ui->killSwitch->setChecked(data.value(KillSwitch) == QLatin1String("yes"));
    ui->allowLan->setChecked(data.value(AllowLan) != QLatin1String("no"));
    ui->dns->setText(data.value(Dns));
}

void PnvProtectionBlock::write(NMStringMap &data) const
{
    pnvSetBool(data, KillSwitch, ui->killSwitch->isChecked());
    pnvSetBool(data, AllowLan, ui->allowLan->isChecked());
    pnvSet(data, Dns, ui->dns->text());
}

void PnvProtectionBlock::setKillSwitchDisabled(const QString &reason)
{
    ui->killSwitch->setEnabled(reason.isEmpty());
    ui->killSwitchReason->setText(reason);
    ui->killSwitchReason->setVisible(!reason.isEmpty());
    updateAllowLan();
}

void PnvProtectionBlock::updateAllowLan()
{
    ui->allowLan->setEnabled(ui->killSwitch->isEnabled() && ui->killSwitch->isChecked());
}

QList<PnvBlock::Field> PnvProtectionBlock::fields() const
{
    return {{KillSwitch, ui->killSwitch->text().remove(QLatin1Char('&'))},
            {AllowLan, ui->allowLan->text().remove(QLatin1Char('&'))},
            {Dns, pnvLabel(ui->dnsLabel)}};
}
