// SPDX-License-Identifier: MIT
#pragma once

#include "block.h"

namespace Ui
{
class ServerBlock;
}

// "Server": server and port; with PortList (Hysteria2) ports in link format instead of port.
class PNV_WIDGETS_EXPORT PnvServerBlock : public PnvBlock
{
    Q_OBJECT
public:
    enum Ports { SinglePort, PortList };

    PnvServerBlock(const QString &service, Ports ports, QWidget *parent = nullptr);
    ~PnvServerBlock() override;

    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

protected:
    QList<Field> fields() const override;

private:
    QString portKey() const;

    std::unique_ptr<Ui::ServerBlock> ui;
    Ports m_ports;
};
