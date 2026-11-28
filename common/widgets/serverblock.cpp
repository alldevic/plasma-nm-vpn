// SPDX-License-Identifier: MIT
#include "serverblock.h"

#include <KLocalizedString>

#include "ui_serverblock.h"
#include "util.h"

PnvServerBlock::PnvServerBlock(const QString &service, Ports ports, QWidget *parent)
    : PnvBlock(service, parent)
    , ui(std::make_unique<Ui::ServerBlock>())
    , m_ports(ports)
{
    ui->setupUi(this);
    if (m_ports == PortList) {
        ui->portLabel->setText(i18nc("@label:textbox", "Ports:"));
        ui->port->setPlaceholderText(i18nc("@info:placeholder", "443 or 443,5000-6000"));
    }
    connect(ui->server, &QLineEdit::textChanged, this, &PnvServerBlock::fieldChanged);
    connect(ui->port, &QLineEdit::textChanged, this, &PnvServerBlock::fieldChanged);
    setProblemLabel(ui->problems);
}

PnvServerBlock::~PnvServerBlock() = default;

QString PnvServerBlock::portKey() const
{
    return m_ports == PortList ? QStringLiteral("ports") : QStringLiteral("port");
}

void PnvServerBlock::load(const NMStringMap &data)
{
    ui->server->setText(data.value(QStringLiteral("server")));
    ui->port->setText(data.value(portKey()));
}

void PnvServerBlock::write(NMStringMap &data) const
{
    pnvSet(data, QStringLiteral("server"), ui->server->text());
    pnvSet(data, portKey(), ui->port->text());
}

QList<PnvBlock::Field> PnvServerBlock::fields() const
{
    return {{QStringLiteral("server"), pnvLabel(ui->serverLabel)}, {portKey(), pnvLabel(ui->portLabel)}};
}
