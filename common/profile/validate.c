/* SPDX-License-Identifier: MIT */
#include <arpa/inet.h>
#include <string.h>

#include "pnv-profile.h"

static gboolean fail(PnvProblem *p, PnvCode code, const char *key, gint64 position, gint64 min, gint64 max)
{
    if (p) {
        pnv_problem_clear(p);
        *p = (PnvProblem){code, g_strdup(key), NULL, position, min, max};
    }
    return FALSE;
}

void pnv_problem_clear(PnvProblem *p)
{
    g_clear_pointer(&p->key, g_free);
    g_clear_pointer(&p->detail, g_free);
    *p = (PnvProblem){PNV_OK, NULL, NULL, -1, 0, 0};
}

gboolean pnv_check_string(const char *key, const char *value, gsize max, PnvProblem *p)
{
    const char *end;

    if (!g_utf8_validate(value, -1, &end))
        return fail(p, PNV_E_NOT_UTF8, key, end - value, 0, 0);
    for (const char *c = value; *c; c++)
        if ((guchar)*c < 0x20 || *c == 0x7f)
            return fail(p, PNV_E_CONTROL_CHAR, key, c - value, 0, 0);
    /* C1 controls (U+0080–U+009F) and the line and paragraph separators break lines just as well. */
    for (const char *c = value; *c; c = g_utf8_next_char(c)) {
        gunichar u = g_utf8_get_char(c);
        if ((u >= 0x80 && u <= 0x9f) || u == 0x2028 || u == 0x2029)
            return fail(p, PNV_E_CONTROL_CHAR, key, c - value, 0, 0);
    }
    if (strlen(value) > max)
        return fail(p, PNV_E_TOO_LONG, key, -1, 0, max);
    return TRUE;
}

static gboolean is_ip4(const char *s)
{
    struct in_addr a;
    return inet_pton(AF_INET, s, &a) == 1;
}

static gboolean is_ip6(const char *s)
{
    struct in6_addr a;
    return inet_pton(AF_INET6, s, &a) == 1;
}

/* A DNS name per RFC 1123 (an optional trailing dot), or an IPv4 or IPv6 literal (no brackets, no zone). */
gboolean pnv_check_host(const char *key, const char *value, PnvProblem *p)
{
    gsize len = strlen(value);
    gboolean numeric = TRUE;
    const char *label = value;

    if (!pnv_check_string(key, value, 253, p))
        return FALSE;
    if (len == 0)
        return fail(p, PNV_E_EMPTY, key, -1, 0, 0);
    if (strchr(value, ':'))
        return is_ip6(value) ? TRUE : fail(p, PNV_E_BAD_HOST, key, 0, 0, 0);
    for (gsize i = 0; i <= len; i++) {
        char c = value[i];
        if (c == '.' || c == '\0') {
            gsize n = value + i - label;
            if (n == 0 && !(c == '\0' && i == len && i > 0 && value[i - 1] == '.'))
                return fail(p, PNV_E_BAD_HOST, key, i, 0, 0);
            if (n > 63)
                return fail(p, PNV_E_BAD_HOST, key, label - value + 63, 0, 0);
            if (n > 0 && (label[0] == '-' || label[n - 1] == '-'))
                return fail(p, PNV_E_BAD_HOST, key, label[0] == '-' ? (gsize)(label - value) : i - 1, 0, 0);
            label = value + i + 1;
        } else if (g_ascii_isalpha(c) || c == '-') {
            numeric = FALSE;
        } else if (!g_ascii_isdigit(c)) {
            return fail(p, PNV_E_BAD_HOST, key, i, 0, 0);
        }
    }
    /* All-numeric labels are an IPv4 address or nothing: 1.2.3.256 is not a name. */
    if (numeric && !is_ip4(value))
        return fail(p, PNV_E_BAD_HOST, key, 0, 0, 0);
    return TRUE;
}

/* Decimal digits only: no sign, no spaces, no leading zeros beyond a lone 0. */
static gboolean parse_decimal(const char *s, gsize n, guint64 *out)
{
    guint64 v = 0;

    if (n == 0 || n > 10 || (n > 1 && s[0] == '0'))
        return FALSE;
    for (gsize i = 0; i < n; i++) {
        if (!g_ascii_isdigit(s[i]))
            return FALSE;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return TRUE;
}

gboolean pnv_parse_uint(const char *key, const char *value, guint min, guint max, guint *out, PnvProblem *p)
{
    guint64 v;

    if (!parse_decimal(value, strlen(value), &v))
        return fail(p, PNV_E_BAD_NUMBER, key, -1, min, max);
    if (v < min || v > max)
        return fail(p, PNV_E_OUT_OF_RANGE, key, -1, min, max);
    *out = v;
    return TRUE;
}

gboolean pnv_parse_port(const char *key, const char *value, guint16 *port, PnvProblem *p)
{
    guint64 v;

    if (!parse_decimal(value, strlen(value), &v) || v < 1 || v > 65535)
        return fail(p, PNV_E_BAD_PORT, key, -1, 1, 65535);
    *port = v;
    return TRUE;
}

/* The Hysteria2 link format: 443 or 443,5000-6000. */
gboolean pnv_parse_ports(const char *key, const char *value, GArray **out, PnvProblem *p)
{
    g_autoptr(GArray) ranges = g_array_new(FALSE, FALSE, sizeof(PnvPortRange));
    const char *item = value;

    if (!pnv_check_string(key, value, 255, p))
        return FALSE;
    for (;;) {
        gsize n = strcspn(item, ",");
        const char *dash = memchr(item, '-', n);
        guint64 first, last;

        if (dash) {
            if (!parse_decimal(item, dash - item, &first) || !parse_decimal(dash + 1, item + n - dash - 1, &last))
                return fail(p, PNV_E_BAD_PORTS, key, item - value, 1, 65535);
        } else if (!parse_decimal(item, n, &first)) {
            return fail(p, PNV_E_BAD_PORTS, key, item - value, 1, 65535);
        } else {
            last = first;
        }
        if (first < 1 || last > 65535)
            return fail(p, PNV_E_BAD_PORTS, key, item - value, 1, 65535);
        if (first > last)
            return fail(p, PNV_E_BAD_PORT_RANGE, key, item - value, 0, 0);
        g_array_append_val(ranges, ((PnvPortRange){first, last}));
        if (item[n] == '\0')
            break;
        item += n + 1;
    }
    if (ranges->len > 32)
        return fail(p, PNV_E_TOO_MANY, key, -1, 0, 32);
    *out = g_steal_pointer(&ranges);
    return TRUE;
}

gboolean pnv_parse_bool(const char *key, const char *value, gboolean *out, PnvProblem *p)
{
    if (strcmp(value, "yes") == 0)
        *out = TRUE;
    else if (strcmp(value, "no") == 0)
        *out = FALSE;
    else
        return fail(p, PNV_E_BAD_BOOL, key, -1, 0, 0);
    return TRUE;
}

static int base64_value(char c)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *at = c ? strchr(alphabet, c) : NULL;
    return at ? at - alphabet : -1;
}

/* Standard alphabet, padded, canonical (unused bits zero), exactly `length` bytes. */
gboolean pnv_parse_base64(const char *key, const char *value, gsize length, GBytes **out, PnvProblem *p)
{
    gsize len = strlen(value), want = (length + 2) / 3 * 4, pad = (3 - length % 3) % 3;
    g_autofree guchar *bytes = NULL;
    gsize n;

    for (gsize i = 0; i < len; i++) {
        gboolean padding = i >= want - pad && i < want;
        if (padding ? value[i] != '=' : base64_value(value[i]) < 0)
            return fail(p, PNV_E_BAD_BASE64, key, i, 0, 0);
    }
    if (len != want)
        return fail(p, PNV_E_BAD_LENGTH, key, -1, length, length);
    bytes = g_base64_decode(value, &n);
    if (pad && (base64_value(value[want - pad - 1]) & (pad == 1 ? 0x3 : 0xf)))
        return fail(p, PNV_E_BAD_BASE64, key, want - pad - 1, 0, 0);
    if (out)
        *out = g_bytes_new(bytes, n);
    return TRUE;
}

/* Hex digits, either case, optionally with ':' between bytes (as in Hysteria2's pinSHA256). */
gboolean pnv_parse_hex(const char *key, const char *value, gsize length, GBytes **out, PnvProblem *p)
{
    g_autoptr(GByteArray) bytes = g_byte_array_new();
    gboolean colons = strchr(value, ':') != NULL;
    const char *c = value;

    while (*c) {
        guint8 b;
        if (!g_ascii_isxdigit(c[0]) || !g_ascii_isxdigit(c[1]))
            return fail(p, PNV_E_BAD_HEX, key, g_ascii_isxdigit(c[0]) ? c - value + 1 : c - value, 0, 0);
        b = g_ascii_xdigit_value(c[0]) << 4 | g_ascii_xdigit_value(c[1]);
        g_byte_array_append(bytes, &b, 1);
        c += 2;
        if (colons && *c) {
            if (*c != ':' || !c[1])
                return fail(p, PNV_E_BAD_HEX, key, c - value, 0, 0);
            c++;
        }
    }
    if (bytes->len != length)
        return fail(p, PNV_E_BAD_LENGTH, key, -1, length, length);
    if (out)
        *out = g_byte_array_free_to_bytes(g_steal_pointer(&bytes));
    return TRUE;
}

/* A comma-separated list; spaces around the items are allowed, empty items are not. */
static GStrv split_list(const char *value)
{
    GStrv items = g_strsplit(value, ",", -1);
    for (GStrv i = items; *i; i++)
        g_strstrip(*i);
    return items;
}

gboolean pnv_parse_ip4_list(const char *key, const char *value, guint max, GPtrArray **out, PnvProblem *p)
{
    g_auto(GStrv) items = split_list(value);
    g_autoptr(GPtrArray) list = g_ptr_array_new_with_free_func(g_free);

    if (!pnv_check_string(key, value, 255, p))
        return FALSE;
    if (!*value)
        return fail(p, PNV_E_BAD_IP, key, 0, 0, 0);
    for (guint i = 0; items[i]; i++) {
        if (!is_ip4(items[i]))
            return fail(p, PNV_E_BAD_IP, key, i, 0, 0);
        g_ptr_array_add(list, g_strdup(items[i]));
    }
    if (list->len > max)
        return fail(p, PNV_E_TOO_MANY, key, -1, 0, max);
    *out = g_steal_pointer(&list);
    return TRUE;
}

static gint cidr_index(GArray *list, PnvCidr4 c)
{
    for (guint i = 0; i < list->len; i++) {
        PnvCidr4 *e = &g_array_index(list, PnvCidr4, i);
        if (e->address == c.address && e->prefix == c.prefix)
            return i;
    }
    return -1;
}

gboolean pnv_parse_cidr4_list(const char *key, const char *value, guint max, GArray **out, PnvProblem *p)
{
    static const PnvCidr4 all = {0, 0}, low = {0, 1}, high = {0x80000000u, 1};
    g_auto(GStrv) items = split_list(value);
    g_autoptr(GArray) parsed = g_array_new(FALSE, FALSE, sizeof(PnvCidr4));
    g_autoptr(GArray) list = g_array_new(FALSE, FALSE, sizeof(PnvCidr4));
    gboolean both;

    if (!pnv_check_string(key, value, 4096, p))
        return FALSE;
    if (!*value)
        return fail(p, PNV_E_BAD_CIDR, key, 0, 0, 0);
    for (guint i = 0; items[i]; i++) {
        g_auto(GStrv) parts = g_strsplit(items[i], "/", 3);
        struct in_addr a;
        guint64 prefix = 32;

        if (!parts[0] || inet_pton(AF_INET, parts[0], &a) != 1 || (parts[1] && parts[2]) ||
            (parts[1] && (!parse_decimal(parts[1], strlen(parts[1]), &prefix) || prefix > 32)))
            return fail(p, PNV_E_BAD_CIDR, key, i, 0, 0);
        g_array_append_val(parsed, ((PnvCidr4){ntohl(a.s_addr), prefix}));
    }
    /* 0.0.0.0/1 + 128.0.0.0/1 (the wg-quick way to win over a default route) is a default route. */
    both = cidr_index(parsed, low) >= 0 && cidr_index(parsed, high) >= 0;
    for (guint i = 0; i < parsed->len; i++) {
        PnvCidr4 c = g_array_index(parsed, PnvCidr4, i);
        if (both && c.prefix == 1 && (c.address == low.address || c.address == high.address))
            c = all;
        if (cidr_index(list, c) < 0)
            g_array_append_val(list, c);
    }
    if (list->len > max)
        return fail(p, PNV_E_TOO_MANY, key, -1, 0, max);
    *out = g_steal_pointer(&list);
    return TRUE;
}

/* ALPN protocol IDs: printable ASCII without spaces or commas, 1–255 bytes each. */
gboolean pnv_parse_alpn(const char *key, const char *value, GPtrArray **out, PnvProblem *p)
{
    g_auto(GStrv) items = split_list(value);
    g_autoptr(GPtrArray) list = g_ptr_array_new_with_free_func(g_free);

    if (!pnv_check_string(key, value, 255, p))
        return FALSE;
    if (!*value)
        return fail(p, PNV_E_BAD_ALPN, key, 0, 0, 0);
    for (guint i = 0; items[i]; i++) {
        if (!items[i][0])
            return fail(p, PNV_E_BAD_ALPN, key, i, 0, 0);
        for (const char *c = items[i]; *c; c++)
            if ((guchar)*c <= 0x20 || (guchar)*c >= 0x7f)
                return fail(p, PNV_E_BAD_ALPN, key, i, 0, 0);
        g_ptr_array_add(list, g_strdup(items[i]));
    }
    if (list->len > 8)
        return fail(p, PNV_E_TOO_MANY, key, -1, 0, 8);
    *out = g_steal_pointer(&list);
    return TRUE;
}
