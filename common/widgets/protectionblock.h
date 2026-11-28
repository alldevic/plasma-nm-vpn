// SPDX-License-Identifier: MIT
#pragma once

#include "block.h"

namespace Ui
{
class ProtectionBlock;
}

// "Protection": kill-switch (default no), allow-lan (default yes), dns. The editor cannot see never-default, so the
// hint next to the kill switch is always shown (design.md, "Leak protection").
class PNV_WIDGETS_EXPORT PnvProtectionBlock : public PnvBlock
{
    Q_OBJECT
public:
    explicit PnvProtectionBlock(const QString &service, QWidget *parent = nullptr);
    ~PnvProtectionBlock() override;

    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

    // For editors that can tell from their own fields that the tunnel is not a default-route tunnel (AmneziaWG
    // allowed-ips): the checkbox is disabled with the reason shown and keeps its value. An empty reason enables it.
    void setKillSwitchDisabled(const QString &reason);

protected:
    QList<Field> fields() const override;

private:
    void updateAllowLan();

    std::unique_ptr<Ui::ProtectionBlock> ui;
};
