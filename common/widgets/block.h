// SPDX-License-Identifier: MIT
// The form blocks the editors are assembled from (design.md, "plasma-nm user interface"). A block reads and writes
// its own vpn.data keys and checks them with the 2.2 validators, the same ones the daemon uses.
#pragma once

#include <NetworkManagerQt/GenericTypes>
#include <QWidget>

#include <memory>

#include "pnv_widgets_export.h"

class QLabel;
struct PnvList;

class PNV_WIDGETS_EXPORT PnvBlock : public QWidget
{
    Q_OBJECT
public:
    explicit PnvBlock(const QString &service, QWidget *parent = nullptr);
    ~PnvBlock() override;

    virtual void load(const NMStringMap &data) = 0;
    // Sets the block's keys in data and removes those left empty; other keys stay as they are.
    virtual void write(NMStringMap &data) const = 0;

    // The rendered messages for what the validators reject; empty when the block is valid.
    QStringList problems() const;
    bool isValid() const;

Q_SIGNALS:
    void changed();

protected:
    struct Field {
        QString key;
        QString label;
    };
    // The fields problems() checks, as write() produces them.
    virtual QList<Field> fields() const = 0;

    QString service() const;
    // Merges a list file of the core package under its prefix; a file that does not load is a problem of the block.
    void loadList(const QString &path, const char *prefix);
    const PnvList *list() const;
    QStringList listEntries(const char *prefix) const;
    // Where problems() is shown, refreshed on every change.
    void setProblemLabel(QLabel *label);
    void fieldChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};
