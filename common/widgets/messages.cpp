// SPDX-License-Identifier: MIT
#include "messages.h"

#include <KLazyLocalizedString>

namespace
{
// args, in placeholder order: k the field, d the detail, p a 0-based position or list index (shown from 1),
// l a line number, n and x the limits.
struct Message {
    PnvCode code;
    KLazyLocalizedString text;
    const char *args;
};

constexpr Message messages[] = {
    // The validators (2.2).
    {PNV_E_UNKNOWN_SERVICE, kli18nc("@info", "The VPN type %1 is not known"), "d"},
    {PNV_E_SCHEMA_TOO_NEW, kli18nc("@info", "The profile is from a newer version of plasma-nm-vpn (this one knows schema %1)"), "x"},
    {PNV_E_UNKNOWN_KEY, kli18nc("@info", "Unknown setting %1"), "k"},
    {PNV_E_UNKNOWN_SECRET, kli18nc("@info", "Unknown secret %1"), "k"},
    {PNV_E_MISSING, kli18nc("@info", "%1: required"), "k"},
    {PNV_E_NOT_UTF8, kli18nc("@info", "%1: invalid text at position %2"), "kp"},
    {PNV_E_CONTROL_CHAR, kli18nc("@info", "%1: control character at position %2"), "kp"},
    {PNV_E_TOO_LONG, kli18nc("@info", "%1: longer than %2 bytes"), "kx"},
    {PNV_E_EMPTY, kli18nc("@info", "%1: empty"), "k"},
    {PNV_E_BAD_HOST, kli18nc("@info", "%1: not a host name or IP address (position %2)"), "kp"},
    {PNV_E_BAD_PORT, kli18nc("@info", "%1: not a port from %2 to %3"), "knx"},
    {PNV_E_BAD_PORTS, kli18nc("@info", "%1: not a list of ports and port ranges (position %2)"), "kp"},
    {PNV_E_BAD_PORT_RANGE, kli18nc("@info", "%1: a port range starts after its end (position %2)"), "kp"},
    {PNV_E_BAD_NUMBER, kli18nc("@info", "%1: not a number"), "k"},
    {PNV_E_OUT_OF_RANGE, kli18nc("@info", "%1: not from %2 to %3"), "knx"},
    {PNV_E_BAD_BOOL, kli18nc("@info", "%1: must be yes or no"), "k"},
    {PNV_E_BAD_ENUM, kli18nc("@info", "%1: not one of the choices"), "k"},
    {PNV_E_BAD_BASE64, kli18nc("@info", "%1: not valid base64 (position %2)"), "kp"},
    {PNV_E_BAD_HEX, kli18nc("@info", "%1: not valid hex (position %2)"), "kp"},
    {PNV_E_BAD_LENGTH, kli18nc("@info", "%1: must be %2 bytes long"), "kx"},
    {PNV_E_BAD_IP, kli18nc("@info", "%1: entry %2 is not an IPv4 address"), "kp"},
    {PNV_E_BAD_CIDR, kli18nc("@info", "%1: entry %2 is not an IPv4 address or subnet"), "kp"},
    {PNV_E_TOO_MANY, kli18nc("@info", "%1: more than %2 entries"), "kx"},
    {PNV_E_BAD_ALPN, kli18nc("@info", "%1: entry %2 is not an ALPN protocol name"), "kp"},
    {PNV_E_BAD_FLAGS, kli18nc("@info", "%1: the secret flags must be from %2 to %3"), "knx"},
    {PNV_E_BAD_JSON, kli18nc("@info", "%1: not valid JSON"), "k"},
    {PNV_E_JSON_NOT_OBJECT, kli18nc("@info", "%1: must be a JSON object"), "k"},
    {PNV_E_JSON_TOO_DEEP, kli18nc("@info", "%1: nested deeper than %2 levels"), "kx"},
    {PNV_E_EXTRA_FORBIDDEN, kli18nc("@info", "%1: %2 is never allowed"), "kd"},
    {PNV_E_EXTRA_NOT_ALLOWED, kli18nc("@info", "%1: %2 is not supported with this core version"), "kd"},
    {PNV_E_EXTRA_BAD_VALUE, kli18nc("@info", "%1: %2 has a value of a kind that is not allowed"), "kd"},
    {PNV_E_LIST_FILE, kli18nc("@info", "Cannot read %1 of the core package"), "d"},
    {PNV_E_LIST_ENTRY, kli18nc("@info", "%1 of the core package has a bad entry on line %2"), "dl"},
    {PNV_E_BAD_PATH, kli18nc("@info", "%1: must start with / and have no spaces"), "k"},
    {PNV_E_BAD_ENCRYPTION, kli18nc("@info", "%1: must be none or a VLESS Encryption key"), "k"},
    {PNV_E_BAD_CHAIN, kli18nc("@info", "%1: a tag chain cannot hold the characters # = [ ] or control characters"), "k"},
    // The import parsers (6.6).
    {PNV_E_IMPORT_FORMAT, kli18nc("@info", "This is not a hysteria2://, hy2:// or vless:// link nor an AmneziaWG configuration"), ""},
    {PNV_E_IMPORT_TOO_LARGE, kli18nc("@info", "The text to import is larger than %1 bytes"), "x"},
    {PNV_E_IMPORT_UNSUPPORTED, kli18nc("@info", "%1 is not supported in this version"), "d"},
    {PNV_E_IMPORT_BAD_LINK, kli18nc("@info the part of a link: host, port, path…", "The link has a malformed %1"), "d"},
    {PNV_E_IMPORT_NOT_REALITY, kli18nc("@info", "The link's %1 is not supported: only XHTTP transport with Reality is"), "k"},
    {PNV_E_IMPORT_PEERS, kli18nc("@info", "The configuration has more than one [Peer]; only one is supported"), ""},
    {PNV_E_IMPORT_BAD_LINE, kli18nc("@info", "The configuration has a malformed line %1"), "l"},
    {PNV_E_IMPORT_DUPLICATE, kli18nc("@info", "%1 is given twice"), "k"},
    {PNV_W_DROPPED, kli18nc("@info", "%1 is not supported here and was dropped"), "k"},
    {PNV_W_WG_QUICK, kli18nc("@info", "%1 is a wg-quick setting and was dropped"), "k"},
    {PNV_W_EXTRA_ADDRESS, kli18nc("@info", "%1: entry %2 was dropped, only the first IPv4 address is used"), "kp"},
    {PNV_W_IPV6, kli18nc("@info", "%1: entry %2 is IPv6 and was dropped"), "kp"},
    {PNV_W_DNS_NAME, kli18nc("@info", "%1: entry %2 is not an IP address and was dropped"), "kp"},
    {PNV_W_TO_EXTRA, kli18nc("@info", "%1 was carried into the additional parameters"), "k"},
};

const Message *find(PnvCode code)
{
    for (const auto &m : messages)
        if (m.code == code)
            return &m;
    return nullptr;
}

// Names come from the profile: cut short like the daemon does for the journal.
QString name(const char *s)
{
    const QString text = QString::fromUtf8(s ? s : "");
    return text.size() > 64 ? text.left(64) + QStringLiteral("…") : text;
}
}

QString pnvProblemMessage(const PnvProblem &p, const QString &label)
{
    const Message *m = find(p.code);
    if (!m)
        return QString::fromLatin1(pnv_code_name(p.code));
    KLocalizedString text = m->text;
    for (const char *a = m->args; *a; a++) {
        switch (*a) {
        case 'k':
            text = text.subs(label.isEmpty() ? name(p.key) : label);
            break;
        case 'd':
            text = text.subs(name(p.detail));
            break;
        case 'p':
            text = text.subs(p.position + 1);
            break;
        case 'l':
            text = text.subs(p.position);
            break;
        case 'n':
            text = text.subs(p.min);
            break;
        case 'x':
            text = text.subs(p.max);
            break;
        }
    }
    return text.toString();
}

bool pnvHasMessage(PnvCode code)
{
    return find(code);
}
