// SPDX-License-Identifier: MIT
// The AmneziaWG editor (6.5): "Interface", "Peer" and a key–value table of the AmneziaWG parameters, whose keys come
// only from the allowlist of plasma-nm-vpn-amneziawg (3.1).
#pragma once

#include "pnv-editor.h"

class PnvProtectionBlock;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

// address: one IPv4 address with its prefix, since Ip4Config holds one.
class AwgInterfaceBlock : public PnvBlock
{
    Q_OBJECT
public:
    explicit AwgInterfaceBlock(QWidget *parent = nullptr);
    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

protected:
    QList<Field> fields() const override;
    QStringList extraProblems() const override;

private:
    QLineEdit *m_address;
    QLabel *m_label;
};

// peer-public-key, allowed-ips (IPv4, normalized by the 2.2 library), keepalive.
class AwgPeerBlock : public PnvBlock
{
    Q_OBJECT
public:
    explicit AwgPeerBlock(QWidget *parent = nullptr);
    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;
    // allowed-ips holds 0.0.0.0/0 (after normalization): a default-route tunnel unless never-default.
    bool defaultRoute() const;

protected:
    QList<Field> fields() const override;
    QStringList extraProblems() const override;

private:
    QString normalizedAllowedIps(bool *defaultRoute = nullptr) const;

    QLineEdit *m_peerKey, *m_allowedIps, *m_keepalive;
    QList<QLabel *> m_labels;
};

// The awg-<parameter> keys: chosen from the allowlist only, each at most once.
class AwgParamsBlock : public PnvBlock
{
    Q_OBJECT
public:
    AwgParamsBlock(const QString &allowlist, QWidget *parent = nullptr);
    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

protected:
    QList<Field> fields() const override;

private:
    struct Row {
        QWidget *widget;
        QComboBox *key;
        QLineEdit *value;
    };
    void addRow(const QString &key = {}, const QString &value = {});
    void removeRow(QWidget *row);
    void refresh();
    QStringList usedKeys(const QComboBox *except = nullptr) const;

    QStringList m_keys;     // vpn.data keys in list order
    QStringList m_names;    // their names in the awg config
    QVBoxLayout *m_rows;
    QPushButton *m_add;
    QList<Row> m_rowList;
    int m_serial = 0;
};

class AwgWidget : public PnvEditorWidget
{
    Q_OBJECT
public:
    // dataDir: the package's allowlist; tests pass the source tree's.
    explicit AwgWidget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent = nullptr,
                       const QString &dataDir = QStringLiteral("/usr/share/plasma-nm-vpn/amneziawg"));

protected:
    PasswordField::PasswordOption secretOption(const QString &key, const PasswordField *field) const override;

private:
    void updateKillSwitch();

    AwgPeerBlock *m_peer;
    PnvProtectionBlock *m_protection;
};

QList<PnvPromptSecret> awgPromptSecrets();
