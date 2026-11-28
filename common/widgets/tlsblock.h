// SPDX-License-Identifier: MIT
#pragma once

#include "block.h"

namespace Ui
{
class TlsBlock;
}

// "TLS": SNI, ALPN, insecure. The fingerprint (protocols over TCP) shows only with the value list of the core
// package; the certificate pin only with pin set, which no MVP protocol does (design.md, "Profile data schema").
class PNV_WIDGETS_EXPORT PnvTlsBlock : public PnvBlock
{
    Q_OBJECT
public:
    PnvTlsBlock(const QString &service, const QString &fingerprintList = {}, bool pin = false,
                QWidget *parent = nullptr);
    ~PnvTlsBlock() override;

    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

protected:
    QList<Field> fields() const override;

private:
    std::unique_ptr<Ui::TlsBlock> ui;
    bool m_fingerprint;
    bool m_pin;
};
