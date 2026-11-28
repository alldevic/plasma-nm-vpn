/* SPDX-License-Identifier: MIT */
#include "pnv-profile.h"

static const struct {
    const char *name;
    const char *text; /* for the journal; %K the key, %D the detail, %P the position, %N/%X the limits */
} codes[] = {
    [PNV_OK] = {"ok", "no problem"},
    [PNV_E_UNKNOWN_SERVICE] = {"unknown-service", "service type %D is not known"},
    [PNV_E_SCHEMA_TOO_NEW] = {"schema-too-new", "profile schema is newer than this version knows (up to %X)"},
    [PNV_E_UNKNOWN_KEY] = {"unknown-key", "unknown key %K"},
    [PNV_E_UNKNOWN_SECRET] = {"unknown-secret", "unknown secret %K"},
    [PNV_E_MISSING] = {"missing", "%K is required"},
    [PNV_E_NOT_UTF8] = {"not-utf8", "%K is not valid UTF-8 at byte %P"},
    [PNV_E_CONTROL_CHAR] = {"control-char", "%K has a control character at byte %P"},
    [PNV_E_TOO_LONG] = {"too-long", "%K is longer than %X bytes"},
    [PNV_E_EMPTY] = {"empty", "%K is empty"},
    [PNV_E_BAD_HOST] = {"bad-host", "%K is not a host name or IP address (byte %P)"},
    [PNV_E_BAD_PORT] = {"bad-port", "%K is not a port %N-%X"},
    [PNV_E_BAD_PORTS] = {"bad-ports", "%K is not a list of ports and port ranges (byte %P)"},
    [PNV_E_BAD_PORT_RANGE] = {"bad-port-range", "%K has a range whose start is greater than its end (byte %P)"},
    [PNV_E_BAD_NUMBER] = {"bad-number", "%K is not a number"},
    [PNV_E_OUT_OF_RANGE] = {"out-of-range", "%K is outside %N-%X"},
    [PNV_E_BAD_BOOL] = {"bad-bool", "%K must be yes or no"},
    [PNV_E_BAD_ENUM] = {"bad-enum", "%K has a value that is not one of the choices"},
    [PNV_E_BAD_BASE64] = {"bad-base64", "%K is not valid base64 (byte %P)"},
    [PNV_E_BAD_HEX] = {"bad-hex", "%K is not valid hex (byte %P)"},
    [PNV_E_BAD_LENGTH] = {"bad-length", "%K must be %X bytes long"},
    [PNV_E_BAD_IP] = {"bad-ip", "%K: entry %P is not an IPv4 address"},
    [PNV_E_BAD_CIDR] = {"bad-cidr", "%K: entry %P is not an IPv4 address or subnet"},
    [PNV_E_TOO_MANY] = {"too-many", "%K has more than %X entries"},
    [PNV_E_BAD_ALPN] = {"bad-alpn", "%K: entry %P is not an ALPN protocol name"},
    [PNV_E_BAD_FLAGS] = {"bad-flags", "%K is not a secret flags value %N-%X"},
    [PNV_E_BAD_JSON] = {"bad-json", "%K is not valid JSON"},
    [PNV_E_JSON_NOT_OBJECT] = {"json-not-object", "%K must be a JSON object"},
    [PNV_E_JSON_TOO_DEEP] = {"json-too-deep", "%K is nested deeper than %X levels"},
    [PNV_E_EXTRA_FORBIDDEN] = {"extra-forbidden", "%K: %D is never allowed"},
    [PNV_E_EXTRA_NOT_ALLOWED] = {"extra-not-allowed", "%K: %D is not on the allowlist"},
    [PNV_E_EXTRA_BAD_VALUE] = {"extra-bad-value", "%K: %D has a value of a kind that is not allowed"},
    [PNV_E_LIST_FILE] = {"list-file", "cannot read the list %D"},
    [PNV_E_LIST_ENTRY] = {"list-entry", "the list %D has a bad entry on line %P"},
    [PNV_E_BAD_PATH] = {"bad-path", "%K is not a path that starts with / and has no spaces"},
    [PNV_E_BAD_ENCRYPTION] = {"bad-encryption", "%K is neither none nor a VLESS Encryption key"},
    [PNV_E_BAD_CHAIN] = {"bad-chain", "%K has a character a tag chain cannot hold"},
    [PNV_E_IMPORT_FORMAT] = {"import-format", "not a hysteria2://, hy2:// or vless:// link nor an AmneziaWG .conf"},
    [PNV_E_IMPORT_TOO_LARGE] = {"import-too-large", "the text to import is larger than %X bytes"},
    [PNV_E_IMPORT_UNSUPPORTED] = {"import-unsupported", "%D is not supported in this version"},
    [PNV_E_IMPORT_BAD_LINK] = {"import-bad-link", "the link has a malformed %D"},
    [PNV_E_IMPORT_NOT_REALITY] = {"import-not-reality", "the link's %K is not supported: only type=xhttp with security=reality"},
    [PNV_E_IMPORT_PEERS] = {"import-peers", "the .conf has more than one [Peer]"},
    [PNV_E_IMPORT_BAD_LINE] = {"import-bad-line", "the .conf has a malformed line %P"},
    [PNV_E_IMPORT_DUPLICATE] = {"import-duplicate", "%K is given twice"},
    [PNV_W_DROPPED] = {"dropped", "%K is not supported here and was dropped"},
    [PNV_W_WG_QUICK] = {"wg-quick", "%K is a wg-quick setting and was dropped"},
    [PNV_W_EXTRA_ADDRESS] = {"extra-address", "%K: entry %P was dropped, only the first IPv4 address is used"},
    [PNV_W_IPV6] = {"ipv6", "%K: entry %P is IPv6 and was dropped"},
    [PNV_W_DNS_NAME] = {"dns-name", "%K: entry %P is not an IP address and was dropped"},
    [PNV_W_TO_EXTRA] = {"to-extra", "%K was carried into the additional parameters"},
    [PNV_W_EXPORT_LOST] = {"export-lost", "%K is not carried by the format and was left out"},
    [PNV_W_EXPORT_NO_SECRET] = {"export-no-secret", "%K is asked every time, so it is not in the export"},
};

const char *pnv_code_name(PnvCode code)
{
    return (guint)code < G_N_ELEMENTS(codes) && codes[code].name ? codes[code].name : "unknown-code";
}

/* Names come from the profile too: escaped and cut short, so that they cannot forge journal lines. */
static void append_name(GString *out, const char *name)
{
    g_autofree char *escaped = g_strescape(name ? name : "", NULL);

    if (g_utf8_strlen(escaped, -1) > 64) {
        g_string_append_len(out, escaped, g_utf8_offset_to_pointer(escaped, 64) - escaped);
        g_string_append(out, "...");
    } else {
        g_string_append(out, escaped);
    }
}

char *pnv_problem_describe(const PnvProblem *p)
{
    const char *text = (guint)p->code < G_N_ELEMENTS(codes) && codes[p->code].text ? codes[p->code].text
                                                                                    : "unknown problem";
    GString *out = g_string_new(NULL);

    for (const char *c = text; *c; c++) {
        if (*c != '%' || !c[1]) {
            g_string_append_c(out, *c);
            continue;
        }
        switch (*++c) {
        case 'K':
            append_name(out, p->key);
            break;
        case 'D':
            append_name(out, p->detail);
            break;
        case 'P':
            g_string_append_printf(out, "%" G_GINT64_FORMAT, p->position);
            break;
        case 'N':
            g_string_append_printf(out, "%" G_GINT64_FORMAT, p->min);
            break;
        case 'X':
            g_string_append_printf(out, "%" G_GINT64_FORMAT, p->max);
            break;
        }
    }
    return g_string_free(out, FALSE);
}
