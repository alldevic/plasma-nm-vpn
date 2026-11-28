// SPDX-License-Identifier: MIT
#pragma once

#include "block.h"

// "Authentication": a label and the secret field. The field is plasma-nm's PasswordField, which the editor in the
// core package creates and passes in, so the block has no vpn.data keys of its own.
class PNV_WIDGETS_EXPORT PnvAuthBlock : public PnvBlock
{
    Q_OBJECT
public:
    PnvAuthBlock(const QString &service, const QString &label, QWidget *secret, QWidget *parent = nullptr);

    void load(const NMStringMap &) override
    {
    }
    void write(NMStringMap &) const override
    {
    }

protected:
    QList<Field> fields() const override
    {
        return {};
    }
};
