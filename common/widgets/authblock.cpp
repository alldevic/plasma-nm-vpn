// SPDX-License-Identifier: MIT
#include "authblock.h"

#include <QFormLayout>
#include <QLabel>

PnvAuthBlock::PnvAuthBlock(const QString &service, const QString &label, QWidget *secret, QWidget *parent)
    : PnvBlock(service, parent)
{
    auto *form = new QFormLayout(this);
    auto *name = new QLabel(label, this);
    name->setObjectName(QStringLiteral("secretLabel"));
    name->setBuddy(secret);
    form->addRow(name, secret);
}
