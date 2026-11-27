/* SPDX-License-Identifier: MIT */
/* Validators and schema (2.2): input → accepted, or rejected with the expected code and parameters. */
#include <string.h>

#include "pnv-profile.h"

typedef struct {
    const char *value;
    PnvCode code;
    gint64 position;
} Case;

#define OK(v) {v, PNV_OK, -1}
#define NO(v, c, pos) {v, c, pos}
#define ANY -2

static void expect(const char *what, const char *value, gboolean ok, const PnvProblem *p, const Case *c)
{
    if (c->code == PNV_OK) {
        if (!ok)
            g_error("%s: \"%s\" rejected: %s", what, g_strescape(value, NULL), pnv_code_name(p->code));
        return;
    }
    if (ok)
        g_error("%s: \"%s\" accepted, want %s", what, g_strescape(value, NULL), pnv_code_name(c->code));
    if (p->code != c->code || (c->position != ANY && p->position != c->position))
        g_error("%s: \"%s\": got %s at %" G_GINT64_FORMAT ", want %s at %" G_GINT64_FORMAT, what,
                g_strescape(value, NULL), pnv_code_name(p->code), p->position, pnv_code_name(c->code), c->position);
}

static void test_string(void)
{
    static const Case cases[] = {
        OK("plain"),
        OK("ünï"),
        NO("a\nb", PNV_E_CONTROL_CHAR, 1),
        NO("a\rb", PNV_E_CONTROL_CHAR, 1),
        NO("\tx", PNV_E_CONTROL_CHAR, 0),
        NO("x\x7f", PNV_E_CONTROL_CHAR, 1),
        NO("a\xc2\x85", PNV_E_CONTROL_CHAR, 1),
        NO("a\xe2\x80\xa8", PNV_E_CONTROL_CHAR, 1),
        NO("ab\xff", PNV_E_NOT_UTF8, 2),
        NO("0123456789a", PNV_E_TOO_LONG, -1),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_auto(PnvProblem) p = {0};
        expect("string", cases[i].value, pnv_check_string("k", cases[i].value, 10, &p), &p, &cases[i]);
    }
}

static void test_host(void)
{
    g_autofree char *label64 = g_strnfill(64, 'a'), *l63 = g_strnfill(63, 'b'), *l61 = g_strnfill(61, 'e');
    g_autofree char *long_name = g_strjoin(".", "a", l63, l63, l63, l61, NULL);
    const Case cases[] = {
        OK("example.com"),
        OK("vpn-1.example.com."),
        OK("localhost"),
        OK("xn--d1acufc.xn--p1ai"),
        OK("198.51.100.1"),
        OK("2001:db8::1"),
        OK("::ffff:198.51.100.1"),
        NO("", PNV_E_EMPTY, -1),
        NO("a..b", PNV_E_BAD_HOST, 2),
        NO(".a", PNV_E_BAD_HOST, 0),
        NO("-a.com", PNV_E_BAD_HOST, 0),
        NO("a-.com", PNV_E_BAD_HOST, 1),
        NO("a_b.com", PNV_E_BAD_HOST, 1),
        NO("exa mple.com", PNV_E_BAD_HOST, 3),
        NO("ex\nample.com", PNV_E_CONTROL_CHAR, 2),
        NO("1.2.3.256", PNV_E_BAD_HOST, 0),
        NO("1.2.3", PNV_E_BAD_HOST, 0),
        NO("[2001:db8::1]", PNV_E_BAD_HOST, 0),
        NO("fe80::1%eth0", PNV_E_BAD_HOST, 0),
        NO("2001:db8::1::2", PNV_E_BAD_HOST, 0),
        NO(label64, PNV_E_BAD_HOST, 63),
        NO(long_name, PNV_E_TOO_LONG, -1),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_auto(PnvProblem) p = {0};
        expect("host", cases[i].value, pnv_check_host("server", cases[i].value, &p), &p, &cases[i]);
    }
}

static void test_port(void)
{
    static const Case cases[] = {
        OK("1"), OK("443"), OK("65535"),
        NO("0", PNV_E_BAD_PORT, -1), NO("65536", PNV_E_BAD_PORT, -1), NO("-1", PNV_E_BAD_PORT, -1),
        NO("080", PNV_E_BAD_PORT, -1), NO("", PNV_E_BAD_PORT, -1), NO("443 ", PNV_E_BAD_PORT, -1),
        NO("+443", PNV_E_BAD_PORT, -1), NO("99999999999", PNV_E_BAD_PORT, -1),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_auto(PnvProblem) p = {0};
        guint16 port;
        expect("port", cases[i].value, pnv_parse_port("port", cases[i].value, &port, &p), &p, &cases[i]);
    }
}

static void test_ports(void)
{
    static const Case cases[] = {
        OK("443"), OK("443,5000-6000"), OK("1-65535"), OK("5000-5000"),
        NO("6000-5000", PNV_E_BAD_PORT_RANGE, 0),
        NO("443,6000-5000", PNV_E_BAD_PORT_RANGE, 4),
        NO("443,,80", PNV_E_BAD_PORTS, 4),
        NO("443,", PNV_E_BAD_PORTS, 4),
        NO(",443", PNV_E_BAD_PORTS, 0),
        NO("0", PNV_E_BAD_PORTS, 0),
        NO("65536", PNV_E_BAD_PORTS, 0),
        NO("1-2-3", PNV_E_BAD_PORTS, 0),
        NO("-5", PNV_E_BAD_PORTS, 0),
        NO("5-", PNV_E_BAD_PORTS, 0),
        NO(" 443", PNV_E_BAD_PORTS, 0),
        NO("443\n", PNV_E_CONTROL_CHAR, 3),
        NO("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33",
           PNV_E_TOO_MANY, -1),
    };
    g_auto(PnvProblem) p = {0};
    g_autoptr(GArray) ranges = NULL;

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_auto(PnvProblem) q = {0};
        g_autoptr(GArray) r = NULL;
        expect("ports", cases[i].value, pnv_parse_ports("ports", cases[i].value, &r, &q), &q, &cases[i]);
    }
    g_assert_true(pnv_parse_ports("ports", "443,5000-6000", &ranges, &p));
    g_assert_cmpuint(ranges->len, ==, 2);
    g_assert_cmpuint(g_array_index(ranges, PnvPortRange, 0).first, ==, 443);
    g_assert_cmpuint(g_array_index(ranges, PnvPortRange, 0).last, ==, 443);
    g_assert_cmpuint(g_array_index(ranges, PnvPortRange, 1).first, ==, 5000);
    g_assert_cmpuint(g_array_index(ranges, PnvPortRange, 1).last, ==, 6000);
}

static void test_numbers(void)
{
    static const Case cases[] = {
        OK("1280"), OK("9000"),
        NO("1279", PNV_E_OUT_OF_RANGE, -1), NO("9001", PNV_E_OUT_OF_RANGE, -1),
        NO("1e3", PNV_E_BAD_NUMBER, -1), NO("0x500", PNV_E_BAD_NUMBER, -1), NO("", PNV_E_BAD_NUMBER, -1),
    };
    gboolean b;

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_auto(PnvProblem) p = {0};
        guint v;
        expect("uint", cases[i].value, pnv_parse_uint("mtu", cases[i].value, 1280, 9000, &v, &p), &p, &cases[i]);
    }
    g_assert_true(pnv_parse_bool("k", "yes", &b, NULL) && b);
    g_assert_true(pnv_parse_bool("k", "no", &b, NULL) && !b);
    g_assert_false(pnv_parse_bool("k", "Yes", &b, NULL));
    g_assert_false(pnv_parse_bool("k", "1", &b, NULL));
}

static void test_encodings(void)
{
    static const Case b64[] = {
        OK("YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWY="),
        NO("YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWY", PNV_E_BAD_LENGTH, -1),
        NO("YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWZn", PNV_E_BAD_BASE64, 43),
        NO("YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWY=YQ==", PNV_E_BAD_BASE64, 46),
        NO("YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWZ=", PNV_E_BAD_BASE64, 42),
        NO("YWJjZGVmZ2hpamtsbW5vcHFy-3R1dnd4eXphYmNkZWY=", PNV_E_BAD_BASE64, 24),
        NO("", PNV_E_BAD_LENGTH, -1),
    };
    static const Case hex[] = {
        OK("ba884517a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a0"),
        OK("BA:88:45:17:A1:F3:D7:A0:E3:B5:D1:E5:A1:F3:D7:A0:E3:B5:D1:E5:A1:F3:D7:A0:E3:B5:D1:E5:A1:F3:D7:A0"),
        NO("ba884517a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a", PNV_E_BAD_HEX, 63),
        NO("ba884517a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7", PNV_E_BAD_LENGTH, -1),
        NO("zz884517a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a0", PNV_E_BAD_HEX, 0),
        NO("BA:88:45:17:A1:F3:D7:A0:E3:B5:D1:E5:A1:F3:D7:A0:E3:B5:D1:E5:A1:F3:D7:A0:E3:B5:D1:E5:A1:F3:D7:A0:",
           PNV_E_BAD_HEX, 95),
        NO("BA88:45", PNV_E_BAD_HEX, 2),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(b64); i++) {
        g_auto(PnvProblem) p = {0};
        g_autoptr(GBytes) out = NULL;
        expect("base64", b64[i].value, pnv_parse_base64("key", b64[i].value, 32, &out, &p), &p, &b64[i]);
        if (out)
            g_assert_cmpuint(g_bytes_get_size(out), ==, 32);
    }
    for (gsize i = 0; i < G_N_ELEMENTS(hex); i++) {
        g_auto(PnvProblem) p = {0};
        g_autoptr(GBytes) out = NULL;
        expect("hex", hex[i].value, pnv_parse_hex("pin", hex[i].value, 32, &out, &p), &p, &hex[i]);
        if (out)
            g_assert_cmpuint(((const guint8 *)g_bytes_get_data(out, NULL))[0], ==, 0xba);
    }
}

static void test_lists(void)
{
    static const Case ip4[] = {
        OK("1.1.1.1"), OK("1.1.1.1, 9.9.9.9"), OK("1.1.1.1,9.9.9.9"),
        NO("1.1.1.1,,9.9.9.9", PNV_E_BAD_IP, 1), NO("2001:db8::1", PNV_E_BAD_IP, 0),
        NO("1.1.1", PNV_E_BAD_IP, 0), NO("01.1.1.1", PNV_E_BAD_IP, 0),
        NO("1.1.1.1,1.1.1.2,1.1.1.3,1.1.1.4,1.1.1.5,1.1.1.6,1.1.1.7,1.1.1.8,1.1.1.9", PNV_E_TOO_MANY, -1),
    };
    static const Case alpn[] = {
        OK("h3"), OK("h3,h2"), OK("h3, h2"),
        NO("h3,,h2", PNV_E_BAD_ALPN, 1), NO("", PNV_E_BAD_ALPN, 0), NO("h\x7f", PNV_E_CONTROL_CHAR, 1),
        NO("h3,\xc3\xa9", PNV_E_BAD_ALPN, 1),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(ip4); i++) {
        g_auto(PnvProblem) p = {0};
        g_autoptr(GPtrArray) out = NULL;
        expect("ip4", ip4[i].value, pnv_parse_ip4_list("dns", ip4[i].value, 8, &out, &p), &p, &ip4[i]);
    }
    for (gsize i = 0; i < G_N_ELEMENTS(alpn); i++) {
        g_auto(PnvProblem) p = {0};
        g_autoptr(GPtrArray) out = NULL;
        expect("alpn", alpn[i].value, pnv_parse_alpn("tls-alpn", alpn[i].value, &out, &p), &p, &alpn[i]);
    }
}

static char *cidrs(const char *value)
{
    g_autoptr(GArray) list = NULL;
    GString *out;

    if (!pnv_parse_cidr4_list("allowed-ips", value, 64, &list, NULL))
        return g_strdup("rejected");
    out = g_string_new(NULL);
    for (guint i = 0; i < list->len; i++) {
        PnvCidr4 c = g_array_index(list, PnvCidr4, i);
        g_string_append_printf(out, "%s%u.%u.%u.%u/%u", i ? "," : "", c.address >> 24, c.address >> 16 & 0xff,
                               c.address >> 8 & 0xff, c.address & 0xff, c.prefix);
    }
    return g_string_free(out, FALSE);
}

static void test_cidr(void)
{
    static const struct {
        const char *in, *out;
    } cases[] = {
        {"0.0.0.0/1,128.0.0.0/1", "0.0.0.0/0"},
        {"128.0.0.0/1, 0.0.0.0/1", "0.0.0.0/0"},
        {"10.0.0.0/8,0.0.0.0/1,192.168.0.0/16,128.0.0.0/1", "10.0.0.0/8,0.0.0.0/0,192.168.0.0/16"},
        {"0.0.0.0/1", "0.0.0.0/1"},
        {"128.0.0.0/1,10.0.0.0/8", "128.0.0.0/1,10.0.0.0/8"},
        {"0.0.0.0/1,0.0.0.0/1,128.0.0.0/1", "0.0.0.0/0"},
        {"0.0.0.0/0,0.0.0.0/1,128.0.0.0/1", "0.0.0.0/0"},
        {"10.0.0.0/8,10.0.0.0/8", "10.0.0.0/8"},
        {"10.1.2.3", "10.1.2.3/32"},
        {"10.0.0.0/33", "rejected"},
        {"10.0.0/8", "rejected"},
        {"10.0.0.0/08", "rejected"},
        {"10.0.0.0/8/8", "rejected"},
        {"10.0.0.0/8,", "rejected"},
        {"::/0", "rejected"},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_autofree char *got = cidrs(cases[i].in);
        if (strcmp(got, cases[i].out) != 0)
            g_error("cidr \"%s\": got %s, want %s", cases[i].in, got, cases[i].out);
    }
}

static PnvList *extra_list(void)
{
    return pnv_list_parse("# test\nup_mbps\ntls\ntls.fragment\nserver_ports\n", "test", NULL);
}

static void test_extra(void)
{
    g_autoptr(PnvList) allowed = extra_list();
    g_autofree char *open = g_strnfill(9, '['), *close = g_strnfill(9, ']'), *a8200 = g_strnfill(8200, 'a');
    g_autofree char *deep = g_strconcat(open, close, NULL);
    g_autofree char *big = g_strdup_printf("{\"up_mbps\":\"%s\"}", a8200);
    const struct {
        const char *json;
        PnvCode code;
        const char *detail;
    } cases[] = {
        {"{\"up_mbps\":10}", PNV_OK},
        {"{}", PNV_OK},
        {"{\"tls\":{\"fragment\":true}}", PNV_OK},
        {"{\"server_ports\":[\"5000:6000\"]}", PNV_OK},
        {"{\"detour\":\"direct\"}", PNV_E_EXTRA_FORBIDDEN, "detour"},
        {"{\"tls\":{\"certificate_path\":\"/etc/shadow\"}}", PNV_E_EXTRA_FORBIDDEN, "tls.certificate_path"},
        {"{\"tls\":{\"path\":\"/etc/shadow\"}}", PNV_E_EXTRA_FORBIDDEN, "tls.path"},
        {"{\"tls\":{\"ech\":{}}}", PNV_E_EXTRA_NOT_ALLOWED, "tls.ech"},
        {"{\"foo\":1}", PNV_E_EXTRA_NOT_ALLOWED, "foo"},
        {"{\"up_mbps\":null}", PNV_E_EXTRA_BAD_VALUE, "up_mbps"},
        {"{\"server_ports\":[{\"detour\":1}]}", PNV_E_EXTRA_BAD_VALUE, "server_ports"},
        {"{\"tls\":{\"fragment\":\"a\\u000ab\"}}", PNV_E_EXTRA_BAD_VALUE, "tls.fragment"},
        {"{\"up_mbps\":1,\n\"tls\":{}}", PNV_E_CONTROL_CHAR},
        {"[1]", PNV_E_JSON_NOT_OBJECT},
        {"\"x\"", PNV_E_JSON_NOT_OBJECT},
        {"{", PNV_E_BAD_JSON},
        {"{\"up_mbps\":1,}", PNV_E_BAD_JSON},
        {deep, PNV_E_JSON_TOO_DEEP},
        {big, PNV_E_TOO_LONG},
    };
    g_auto(PnvProblem) none = {0};

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_auto(PnvProblem) p = {0};
        g_autoptr(JsonObject) out = NULL;
        gboolean ok = pnv_parse_extra("extra", cases[i].json, allowed, &out, &p);
        Case c = {cases[i].json, cases[i].code, ANY};

        expect("extra", cases[i].json, ok, &p, &c);
        if (cases[i].detail)
            g_assert_cmpstr(p.detail, ==, cases[i].detail);
        g_assert_true(ok == (out != NULL));
    }
    g_assert_false(pnv_parse_extra("extra", "{}", NULL, NULL, &none));
    g_assert_cmpint(none.code, ==, PNV_E_EXTRA_NOT_ALLOWED);
}

static void test_list_file(void)
{
    g_auto(PnvProblem) p = {0};
    g_autoptr(PnvList) list = pnv_list_parse("# c\n\n  chrome  \nfirefox\nchrome\n", "fp", &p);
    g_autoptr(PnvList) bad = pnv_list_parse("ok\n\nnot/ok\n", "fp", &p);

    g_assert_nonnull(list);
    g_assert_cmpuint(list->entries->len, ==, 2);
    g_assert_cmpstr(list->entries->pdata[0], ==, "chrome");
    g_assert_true(pnv_list_contains(list, "firefox"));
    g_assert_false(pnv_list_contains(list, "# c"));
    g_assert_null(bad);
    g_assert_cmpint(p.code, ==, PNV_E_LIST_ENTRY);
    g_assert_cmpint(p.position, ==, 3);
    g_assert_null(pnv_list_load("/nonexistent/list", &p));
    g_assert_cmpint(p.code, ==, PNV_E_LIST_FILE);
}

/* "key=value" pairs; a value may hold anything but NUL. */
static GHashTable *table(const char *const *pairs)
{
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (; pairs && *pairs; pairs++) {
        const char *eq = strchr(*pairs, '=');
        g_hash_table_insert(t, g_strndup(*pairs, eq - *pairs), g_strdup(eq + 1));
    }
    return t;
}

#define PASSWORD "s3cret\npass"

static PnvProfile *parse(const char *const *data, const char *const *secrets, PnvProblem *p)
{
    g_autoptr(GHashTable) d = table(data), s = table(secrets);
    g_autoptr(PnvList) allowed = extra_list();
    return pnv_profile_parse(PNV_SERVICE_HYSTERIA2, d, s, allowed, p);
}

static void test_profile(void)
{
    static const char *const base[] = {"schema=1", "server=vpn.example.com", "ports=443", NULL};
    static const char *const password[] = {"password=pw", NULL};
    static const struct {
        const char *extra_data, *secret;
        PnvCode code;
        const char *key;
    } cases[] = {
        {NULL, NULL, PNV_OK},
        {"server=vpn.example.com\nInjected=1", NULL, PNV_E_CONTROL_CHAR, "server"},
        {"tls-sni=a.example.com\r\nx", NULL, PNV_E_CONTROL_CHAR, "tls-sni"},
        {"tls-sni=198.51.100.1", NULL, PNV_E_BAD_HOST, "tls-sni"},
        {NULL, "password=" PASSWORD, PNV_E_CONTROL_CHAR, "password"},
        {"unknown=1", NULL, PNV_E_UNKNOWN_KEY, "unknown"},
        {"port=443", NULL, PNV_E_UNKNOWN_KEY, "port"},
        {NULL, "token=x", PNV_E_UNKNOWN_SECRET, "token"},
        {"schema=2", NULL, PNV_E_SCHEMA_TOO_NEW, "schema"},
        {"schema=0", NULL, PNV_E_OUT_OF_RANGE, "schema"},
        {"ports=", NULL, PNV_E_MISSING, "ports"},
        {"server=", NULL, PNV_E_MISSING, "server"},
        {"ports=6000-5000", NULL, PNV_E_BAD_PORT_RANGE, "ports"},
        {"ports=443,,80", NULL, PNV_E_BAD_PORTS, "ports"},
        {NULL, "password=", PNV_E_MISSING, "password"},
        {"obfs=salamander", NULL, PNV_E_MISSING, "obfs-password"},
        {"obfs=salamander", "obfs-password=o", PNV_OK},
        {"obfs=xor", NULL, PNV_E_BAD_ENUM, "obfs"},
        {"password-flags=2", NULL, PNV_OK},
        {"password-flags=8", NULL, PNV_E_BAD_FLAGS, "password-flags"},
        {"server-flags=1", NULL, PNV_E_UNKNOWN_KEY, "server-flags"},
        {"extra={\"detour\":\"direct\"}", NULL, PNV_E_EXTRA_FORBIDDEN, "extra"},
        {"extra={\"tls\":{\"key_path\":\"/root/k\"}}", NULL, PNV_E_EXTRA_FORBIDDEN, "extra"},
        {"extra={\"brutal\":1}", NULL, PNV_E_EXTRA_NOT_ALLOWED, "extra"},
        {"kill-switch=on", NULL, PNV_E_BAD_BOOL, "kill-switch"},
        {"mtu=70000", NULL, PNV_E_OUT_OF_RANGE, "mtu"},
        {"hop-interval=1", NULL, PNV_E_OUT_OF_RANGE, "hop-interval"},
        {"up-mbps=0", NULL, PNV_E_OUT_OF_RANGE, "up-mbps"},
        {"tls-pin-sha256=abcd", NULL, PNV_E_BAD_LENGTH, "tls-pin-sha256"},
        {"dns=1.1.1.1,example.com", NULL, PNV_E_BAD_IP, "dns"},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        const char *data[8] = {base[0], base[1], base[2]}, *secrets[4] = {password[0]};
        g_auto(PnvProblem) p = {0};
        g_autoptr(PnvProfile) profile = NULL;
        g_autofree char *text = NULL;

        if (cases[i].extra_data) {
            /* Replaces the base value of the same key. */
            gsize n = strcspn(cases[i].extra_data, "=");
            gsize j = 0;
            while (data[j] && strncmp(data[j], cases[i].extra_data, n + 1) != 0)
                j++;
            data[j] = cases[i].extra_data;
        }
        if (cases[i].secret) {
            gsize n = strcspn(cases[i].secret, "=");
            secrets[strncmp(secrets[0], cases[i].secret, n + 1) == 0 ? 0 : 1] = cases[i].secret;
        }
        profile = parse(data, secrets, &p);
        if (cases[i].code == PNV_OK) {
            if (!profile)
                g_error("profile case %zu rejected: %s", i, pnv_code_name(p.code));
            continue;
        }
        if (profile)
            g_error("profile case %zu accepted, want %s", i, pnv_code_name(cases[i].code));
        if (p.code != cases[i].code || g_strcmp0(p.key, cases[i].key) != 0)
            g_error("profile case %zu: got %s %s, want %s %s", i, pnv_code_name(p.code), p.key,
                    pnv_code_name(cases[i].code), cases[i].key);
        text = pnv_problem_describe(&p);
        g_assert_null(strstr(text, "s3cret"));
        g_assert_null(strchr(text, '\n'));
    }
}

static void test_profile_fields(void)
{
    static const char *const data[] = {
        "server=2001:db8::1", "ports=443,20000-20100", "hop-interval=10", "obfs=salamander",
        "tls-sni=sni.example.com", "tls-alpn=h3", "tls-insecure=yes", "up-mbps=50", "down-mbps=200",
        "dns=203.0.113.1", "kill-switch=yes", "allow-lan=no", "mtu=1400", "extra={\"up_mbps\":5}",
        "password-flags=1", "obfs-password-flags=0", NULL,
    };
    static const char *const secrets[] = {"password=pw", "obfs-password=op", NULL};
    g_auto(PnvProblem) p = {0};
    g_autoptr(PnvProfile) profile = parse(data, secrets, &p);
    const PnvHysteria2 *h;

    g_assert_nonnull(profile);
    h = &profile->hysteria2;
    g_assert_cmpstr(profile->server, ==, "2001:db8::1");
    g_assert_cmpuint(profile->schema, ==, 1);
    g_assert_cmpuint(h->ports->len, ==, 2);
    g_assert_cmpuint(h->hop_interval, ==, 10);
    g_assert_true(h->obfs_salamander);
    g_assert_cmpstr(h->tls_sni, ==, "sni.example.com");
    g_assert_cmpstr(h->tls_alpn->pdata[0], ==, "h3");
    g_assert_true(h->tls_insecure);
    g_assert_cmpuint(h->up_mbps, ==, 50);
    g_assert_cmpuint(h->down_mbps, ==, 200);
    g_assert_cmpstr(h->password, ==, "pw");
    g_assert_cmpstr(h->obfs_password, ==, "op");
    g_assert_cmpstr(profile->dns->pdata[0], ==, "203.0.113.1");
    g_assert_true(profile->kill_switch);
    g_assert_false(profile->allow_lan);
    g_assert_cmpuint(profile->mtu, ==, 1400);
    g_assert_true(json_object_has_member(profile->extra, "up_mbps"));
}

static void test_defaults_and_fields(void)
{
    static const char *const data[] = {"server=198.51.100.1", "ports=443", NULL};
    static const char *const secrets[] = {"password=pw", NULL};
    g_auto(PnvProblem) p = {0};
    g_autoptr(PnvProfile) profile = parse(data, secrets, &p);

    g_assert_nonnull(profile);
    g_assert_cmpuint(profile->schema, ==, 1);
    g_assert_false(profile->kill_switch);
    g_assert_true(profile->allow_lan);
    g_assert_null(profile->dns);
    g_assert_cmpuint(profile->mtu, ==, 0);
    g_assert_cmpuint(profile->hysteria2.hop_interval, ==, 30);
    g_assert_null(profile->hysteria2.tls_sni);

    g_assert_true(pnv_check_field(PNV_SERVICE_HYSTERIA2, "ports", "443,5000-6000", NULL, &p));
    g_assert_false(pnv_check_field(PNV_SERVICE_HYSTERIA2, "ports", "6000-5000", NULL, &p));
    g_assert_cmpint(p.code, ==, PNV_E_BAD_PORT_RANGE);
    g_assert_true(pnv_check_field(PNV_SERVICE_HYSTERIA2, "tls-sni", "", NULL, &p));
    g_assert_false(pnv_check_field(PNV_SERVICE_HYSTERIA2, "server", "", NULL, &p));
    g_assert_cmpint(p.code, ==, PNV_E_MISSING);
    g_assert_false(pnv_check_field(PNV_SERVICE_HYSTERIA2, "nope", "1", NULL, &p));
    g_assert_cmpint(p.code, ==, PNV_E_UNKNOWN_KEY);
    g_assert_false(pnv_check_field("org.example.other", "server", "x", NULL, &p));
    g_assert_cmpint(p.code, ==, PNV_E_UNKNOWN_SERVICE);
    g_assert_null(pnv_profile_parse("org.example.other", NULL, NULL, NULL, &p));
}

static void test_describe(void)
{
    g_auto(PnvProblem) p = {PNV_E_UNKNOWN_KEY, g_strdup("evil\nkey"), NULL, -1, 0, 0};
    g_autofree char *text = pnv_problem_describe(&p);
    g_auto(PnvProblem) q = {PNV_E_OUT_OF_RANGE, g_strdup("mtu"), NULL, -1, 1280, 9000};
    g_autofree char *range = pnv_problem_describe(&q);

    g_assert_cmpstr(text, ==, "unknown key evil\\nkey");
    g_assert_cmpstr(range, ==, "mtu is outside 1280-9000");
    for (int c = PNV_OK; c <= PNV_E_LIST_ENTRY; c++)
        g_assert_cmpstr(pnv_code_name(c), !=, "unknown-code");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/profile/string", test_string);
    g_test_add_func("/profile/host", test_host);
    g_test_add_func("/profile/port", test_port);
    g_test_add_func("/profile/ports", test_ports);
    g_test_add_func("/profile/numbers", test_numbers);
    g_test_add_func("/profile/encodings", test_encodings);
    g_test_add_func("/profile/lists", test_lists);
    g_test_add_func("/profile/cidr", test_cidr);
    g_test_add_func("/profile/extra", test_extra);
    g_test_add_func("/profile/list-file", test_list_file);
    g_test_add_func("/profile/profile", test_profile);
    g_test_add_func("/profile/fields", test_profile_fields);
    g_test_add_func("/profile/defaults", test_defaults_and_fields);
    g_test_add_func("/profile/describe", test_describe);
    return g_test_run();
}
