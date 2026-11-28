// SPDX-License-Identifier: MIT
#pragma once

#include "block.h"

namespace Ui
{
class AdvancedBlock;
}

// "Advanced": mtu, and extra (sing-box and xray) when the editor names the allowlist of the core package.
class PNV_WIDGETS_EXPORT PnvAdvancedBlock : public PnvBlock
{
    Q_OBJECT
public:
    PnvAdvancedBlock(const QString &service, const QString &extraAllowlist = {}, QWidget *parent = nullptr);
    ~PnvAdvancedBlock() override;

    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

protected:
    QList<Field> fields() const override;

private:
    std::unique_ptr<Ui::AdvancedBlock> ui;
    bool m_extra;
};
