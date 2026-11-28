// SPDX-License-Identifier: MIT
// The VLESS XHTTP Reality editor (6.4): the 6.1 blocks without "TLS", the encryption field, and its own "Reality"
// and "XHTTP transport" blocks, whose choices come from the value lists of plasma-nm-vpn-xray (4.1).
#pragma once

#include "pnv-editor.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

// A choice from a value list: the entries in file order, the first one for an empty value (the lists put the
// core's default first); a value outside the list stays, for the validator to name.
class VlessChoice
{
public:
    static void fill(QComboBox *box, const QStringList &entries);
    static void select(QComboBox *box, const QString &value);
};

// encryption: "none" for a new profile, otherwise kept exactly as entered or loaded.
class VlessEncryptionBlock : public PnvBlock
{
    Q_OBJECT
public:
    explicit VlessEncryptionBlock(QWidget *parent = nullptr);
    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

protected:
    QList<Field> fields() const override;

private:
    QLineEdit *m_encryption;
    QLabel *m_label;
};

// "Reality": tls-sni, reality-pbk, reality-sid, reality-spx, tls-fingerprint.
class VlessRealityBlock : public PnvBlock
{
    Q_OBJECT
public:
    VlessRealityBlock(const QString &fingerprintList, QWidget *parent = nullptr);
    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

protected:
    QList<Field> fields() const override;

private:
    QLineEdit *m_sni, *m_pbk, *m_sid, *m_spx;
    QComboBox *m_fingerprint;
    QList<QLabel *> m_labels;
};

// "XHTTP transport": xhttp-mode, xhttp-path, xhttp-host, xhttp-extra.
class VlessXhttpBlock : public PnvBlock
{
    Q_OBJECT
public:
    VlessXhttpBlock(const QString &modeList, const QString &extraAllowlist, QWidget *parent = nullptr);
    void load(const NMStringMap &data) override;
    void write(NMStringMap &data) const override;

protected:
    QList<Field> fields() const override;

private:
    QComboBox *m_mode;
    QLineEdit *m_path, *m_host;
    QPlainTextEdit *m_extra;
    QList<QLabel *> m_labels;
};

class VlessWidget : public PnvEditorWidget
{
    Q_OBJECT
public:
    // dataDir: the package's value lists and allowlists; tests pass the source tree's.
    explicit VlessWidget(const NetworkManager::VpnSetting::Ptr &setting, QWidget *parent = nullptr,
                         const QString &dataDir = QStringLiteral("/usr/share/plasma-nm-vpn/xray"));
};

QList<PnvPromptSecret> vlessPromptSecrets();
