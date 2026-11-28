// SPDX-License-Identifier: MIT
#include "block.h"

#include <QLabel>

#include "messages.h"

struct PnvBlock::Private {
    QString service;
    PnvList *list = pnv_list_new();
    QStringList listProblems;
    QLabel *problemLabel = nullptr;

    ~Private()
    {
        pnv_list_free(list);
    }
};

PnvBlock::PnvBlock(const QString &service, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->service = service;
}

PnvBlock::~PnvBlock() = default;

QString PnvBlock::service() const
{
    return d->service;
}

void PnvBlock::loadList(const QString &path, const char *prefix)
{
    g_auto(PnvProblem) problem = {};
    if (!pnv_list_merge_file(d->list, path.toUtf8().constData(), prefix, &problem))
        d->listProblems << pnvProblemMessage(problem);
}

const PnvList *PnvBlock::list() const
{
    return d->list;
}

QStringList PnvBlock::listEntries(const char *prefix) const
{
    QStringList entries;
    const QByteArray p(prefix);
    for (guint i = 0; i < d->list->entries->len; i++) {
        const QByteArray entry(static_cast<const char *>(g_ptr_array_index(d->list->entries, i)));
        if (entry.startsWith(p))
            entries << QString::fromUtf8(entry.mid(p.size()));
    }
    return entries;
}

QStringList PnvBlock::problems() const
{
    QStringList out = d->listProblems;
    NMStringMap data;
    checkedValues(data);
    const QByteArray service = d->service.toUtf8();
    for (const Field &f : fields()) {
        g_auto(PnvProblem) problem = {};
        const QByteArray key = f.key.toUtf8(), value = data.value(f.key).toUtf8();
        if (!pnv_check_field(service.constData(), key.constData(), value.constData(), d->list, &problem))
            out << pnvProblemMessage(problem, f.label);
    }
    return out + extraProblems();
}

bool PnvBlock::isValid() const
{
    return problems().isEmpty();
}

void PnvBlock::setProblemLabel(QLabel *label)
{
    d->problemLabel = label;
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    fieldChanged();
}

void PnvBlock::fieldChanged()
{
    if (d->problemLabel) {
        const QStringList list = problems();
        d->problemLabel->setText(list.join(QLatin1Char('\n')));
        d->problemLabel->setVisible(!list.isEmpty());
    }
    Q_EMIT changed();
}
