// SPDX-License-Identifier: MIT
#pragma once

#include "block.h"

#include <functional>

// "Authentication": a label and the secret field. The field is plasma-nm's PasswordField, which the editor in the
// core package creates and passes in, so the block writes no vpn.data keys; given the secret's key and a way to
// read the field, it checks the value with the 2.2 validators (the editor calls secretChanged() on each change).
class PNV_WIDGETS_EXPORT PnvAuthBlock : public PnvBlock
{
    Q_OBJECT
public:
    PnvAuthBlock(const QString &service, const QString &label, QWidget *secret, const QString &key = {},
                 std::function<QString()> text = {}, QWidget *parent = nullptr);

    void load(const NMStringMap &) override
    {
    }
    void write(NMStringMap &) const override
    {
    }
    void secretChanged();

protected:
    QList<Field> fields() const override;
    void checkedValues(NMStringMap &data) const override;

private:
    QString m_key, m_label;
    std::function<QString()> m_text;
};
