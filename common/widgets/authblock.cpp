// SPDX-License-Identifier: MIT
#include "authblock.h"

#include <QFormLayout>
#include <QLabel>

PnvAuthBlock::PnvAuthBlock(const QString &service, const QString &label, QWidget *secret, const QString &key,
                           std::function<QString()> text, QWidget *parent)
    : PnvBlock(service, parent)
    , m_key(key)
    , m_text(std::move(text))
{
    auto *form = new QFormLayout(this);
    auto *name = new QLabel(label, this);
    name->setObjectName(QStringLiteral("secretLabel"));
    name->setBuddy(secret);
    form->addRow(name, secret);
    m_label = label.endsWith(QLatin1Char(':')) ? label.chopped(1) : label;
    auto *problems = new QLabel(this);
    problems->setObjectName(QStringLiteral("problems"));
    form->addRow(problems);
    setProblemLabel(problems);
}

void PnvAuthBlock::secretChanged()
{
    fieldChanged();
}

QList<PnvBlock::Field> PnvAuthBlock::fields() const
{
    if (m_key.isEmpty() || !m_text)
        return {};
    return {{m_key, m_label}};
}

void PnvAuthBlock::checkedValues(NMStringMap &data) const
{
    if (!m_key.isEmpty() && m_text)
        data.insert(m_key, m_text());
}
