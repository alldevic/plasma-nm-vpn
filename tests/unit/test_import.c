/* SPDX-License-Identifier: MIT */
/* The import parsers (6.6): the bench's links and .conf, the AmneziaWG rules, the unsupported features and the
 * attack vectors. Usage: test-import <source dir> */
#include <string.h>

#include "pnv-profile.h"

static const char *source;

#define KEY_A "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWY="
#define KEY_B "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDE="

/* The lists each daemon loads, from the source tree. */
static PnvList *lists_for(const char *service)
{
    static const struct {
        const char *service, *prefix, *path;
    } files[] = {
        {PNV_SERVICE_HYSTERIA2, "extra/", "backends/sb/data/hysteria2-extra.list"},
        {PNV_SERVICE_VLESS, "extra/", "backends/xray/data/vless-extra.list"},
        {PNV_SERVICE_VLESS, "xhttp-extra/", "backends/xray/data/xhttp-extra.list"},
        {PNV_SERVICE_VLESS, "tls-fingerprint/", "backends/xray/data/tls-fingerprint.list"},
        {PNV_SERVICE_VLESS, "xhttp-mode/", "backends/xray/data/xhttp-mode.list"},
        {PNV_SERVICE_AMNEZIAWG, "awg/", "backends/awg/data/amneziawg-params.list"},
    };
    PnvList *lists = pnv_list_new();
    for (guint i = 0; i < G_N_ELEMENTS(files); i++) {
        g_autofree char *path = g_build_filename(source, files[i].path, NULL);
        if (g_str_equal(files[i].service, service))
            g_assert_true(pnv_list_merge_file(lists, path, files[i].prefix, NULL));
    }
    return lists;
}

static gboolean in_groups(PnvCode code)
{
    return code >= PNV_CODES_VALIDATOR_FIRST && code <= PNV_CODES_PARSER_LAST;
}

static PnvImport *import(const char *text, PnvProblem *p)
{
    const char *service = pnv_import_service(text);
    g_autoptr(PnvList) lists = lists_for(service ? service : PNV_SERVICE_HYSTERIA2);
    PnvImport *result = pnv_import(text, lists, p);

    if (!result)
        g_assert_true(in_groups(p->code));
    for (guint i = 0; result && i < result->warnings->len; i++)
        g_assert_true(in_groups(g_array_index(result->warnings, PnvProblem, i).code));
    return result;
}

static PnvImport *import_ok(const char *text)
{
    g_auto(PnvProblem) p = {0};
    PnvImport *result = import(text, &p);
    if (!result)
        g_error("import failed: %s (%s)", pnv_code_name(p.code), p.key ? p.key : "");
    return result;
}

static void import_fails(const char *text, PnvCode code)
{
    g_auto(PnvProblem) p = {0};
    g_autoptr(PnvImport) result = import(text, &p);
    if (result)
        g_error("imported, expected %s: %.80s", pnv_code_name(code), text);
    if (p.code != code)
        g_error("expected %s, got %s (%s)", pnv_code_name(code), pnv_code_name(p.code), p.key ? p.key : "");
}

static char *read_data(const char *name)
{
    g_autofree char *path = g_build_filename(source, "tests/unit/data/import", name, NULL);
    char *text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    return text;
}

static const char *data(PnvImport *i, const char *key)
{
    return g_hash_table_lookup(i->data, key);
}

static const char *secret(PnvImport *i, const char *key)
{
    return g_hash_table_lookup(i->secrets, key);
}

/* The warnings as "code:key:position", in order. */
static char *warnings(PnvImport *i)
{
    GString *out = g_string_new(NULL);
    for (guint n = 0; n < i->warnings->len; n++) {
        const PnvProblem *w = &g_array_index(i->warnings, PnvProblem, n);
        g_string_append_printf(out, "%s%s:%s:%" G_GINT64_FORMAT, n ? " " : "", pnv_code_name(w->code),
                               w->key ? w->key : "", w->position);
    }
    return g_string_free(out, FALSE);
}

static void test_bench_hysteria2(void)
{
    g_autofree char *text = read_data("bench-hysteria2.txt"), *hop = read_data("bench-hysteria2-hop.txt");
    g_autoptr(PnvImport) i = import_ok(text), h = import_ok(hop);

    g_assert_cmpstr(i->service, ==, PNV_SERVICE_HYSTERIA2);
    g_assert_cmpstr(data(i, "server"), ==, "srv.stand.test");
    g_assert_cmpstr(data(i, "ports"), ==, "443");
    g_assert_cmpstr(data(i, "obfs"), ==, "salamander");
    g_assert_cmpstr(data(i, "tls-sni"), ==, "srv.stand.test");
    g_assert_cmpstr(data(i, "tls-insecure"), ==, "yes");
    g_assert_cmpstr(secret(i, "password"), ==, "4cfe6b51c9a1f0d2e7b38a6c5d4e3f21");
    g_assert_cmpstr(secret(i, "obfs-password"), ==, "79b63d51a9800c5338f711cff5890afa");
    g_assert_cmpuint(i->warnings->len, ==, 0);
    g_assert_cmpstr(data(h, "ports"), ==, "443,20000-20100");
    g_assert_cmpstr(data(h, "server"), ==, "198.51.100.1");
}

static void test_hysteria2_details(void)
{
    g_autoptr(PnvImport) plain = import_ok("hy2://secret@[2001:db8::1]?sni=a.example");
    g_autoptr(PnvImport) unknown = import_ok("hysteria2://x@h.example:443?upmbps=10&udp_fragment=true&fastopen=1");
    g_autofree char *w = warnings(unknown);

    /* The scheme's default port; an IPv6 host without its brackets. */
    g_assert_cmpstr(data(plain, "ports"), ==, "443");
    g_assert_cmpstr(data(plain, "server"), ==, "2001:db8::1");
    /* A parameter the allowlist names goes into extra, the others are dropped with a warning. */
    g_assert_cmpstr(data(unknown, "extra"), ==, "{\"udp_fragment\":\"true\"}");
    g_assert_cmpstr(w, ==, "dropped:upmbps:0 to-extra:udp_fragment:0 dropped:fastopen:0");
}

static void test_hysteria2_unsupported(void)
{
    import_fails("hysteria2://x@h.example:443?obfs=gecko&obfs-password=y", PNV_E_IMPORT_UNSUPPORTED);
    import_fails("hysteria2+realm://x@h.example:443", PNV_E_IMPORT_UNSUPPORTED);
    import_fails("hy2://x@h.example:443?pinSHA256=" "deadbeef", PNV_E_IMPORT_UNSUPPORTED);
    import_fails("hy2://x@h.example:443?ech=AEX", PNV_E_IMPORT_UNSUPPORTED);
    import_fails("hy2://x@h.example:443?obfs=xor", PNV_E_BAD_ENUM);
}

static void test_bench_vless(void)
{
    g_autofree char *text = read_data("bench-vless.txt");
    g_autoptr(PnvImport) i = import_ok(text);

    g_assert_cmpstr(i->service, ==, PNV_SERVICE_VLESS);
    g_assert_cmpstr(data(i, "server"), ==, "198.51.100.1");
    g_assert_cmpstr(data(i, "port"), ==, "443");
    g_assert_cmpstr(data(i, "encryption"), ==, "none");
    g_assert_cmpstr(data(i, "reality-pbk"), ==, "6lgzU9D_m5zHBBWGxPuMXCYjkmwrlbWLh4LyUOoOdXA");
    g_assert_cmpstr(data(i, "reality-spx"), ==, "/");
    g_assert_cmpstr(data(i, "xhttp-path"), ==, "/3042d8a111ff8f73");
    g_assert_cmpstr(data(i, "tls-fingerprint"), ==, "chrome");
    g_assert_cmpstr(secret(i, "uuid"), ==, "a3f1c2d4-5b6e-4f70-8a9b-0c1d2e3f4a59");
}

static void test_vless_details(void)
{
    const char *base = "vless://a3f1c2d4-5b6e-4f70-8a9b-0c1d2e3f4a59@198.51.100.1:443?type=xhttp&security=reality"
                       "&pbk=6lgzU9D_m5zHBBWGxPuMXCYjkmwrlbWLh4LyUOoOdXA&sni=www.stand.test";
    g_autofree char *dropped = g_strconcat(base, "&pcs=ab&vcn=x&allowInsecure=1&flow=xtls-rprx-vision", NULL);
    g_autofree char *extra = g_strconcat(base, "&extra=%7B%22xPaddingBytes%22%3A%22100-1000%22%7D", NULL);
    g_autofree char *bad_extra = g_strconcat(base, "&extra=%7B%22noSuchKey%22%3A1%7D", NULL);
    g_autofree char *path_extra = g_strconcat(base, "&extra=%7B%22path%22%3A%22%2Fetc%22%7D", NULL);
    g_autoptr(PnvImport) d = import_ok(dropped), e = import_ok(extra);
    g_autofree char *w = warnings(d);

    g_assert_cmpstr(w, ==, "dropped:pcs:0 dropped:vcn:0 dropped:allowInsecure:0 dropped:flow:0");
    g_assert_cmpstr(data(e, "xhttp-extra"), ==, "{\"xPaddingBytes\":\"100-1000\"}");
    import_fails(bad_extra, PNV_E_EXTRA_NOT_ALLOWED);
    import_fails(path_extra, PNV_E_EXTRA_FORBIDDEN);
    import_fails("vless://u@h.example:443?security=reality", PNV_E_IMPORT_NOT_REALITY);
    import_fails("vless://u@h.example:443?type=tcp&security=reality", PNV_E_IMPORT_NOT_REALITY);
    import_fails("vless://u@h.example:443?type=xhttp&security=tls", PNV_E_IMPORT_NOT_REALITY);
    /* No secret: an export without secrets imports with the field empty. */
    {
        g_autofree char *no_uuid = g_strdup(base);
        memmove(strstr(no_uuid, "a3f1"), strstr(no_uuid, "198."), strlen(strstr(no_uuid, "198.")) + 1);
        g_autoptr(PnvImport) n = import_ok(no_uuid);
        g_assert_null(secret(n, "uuid"));
    }
}

static void test_bench_amneziawg(void)
{
    g_autofree char *text = read_data("bench-amneziawg.conf");
    g_autoptr(PnvImport) i = import_ok(text);

    g_assert_cmpstr(i->service, ==, PNV_SERVICE_AMNEZIAWG);
    g_assert_cmpstr(data(i, "server"), ==, "198.51.100.1");
    g_assert_cmpstr(data(i, "port"), ==, "51820");
    g_assert_cmpstr(data(i, "address"), ==, "100.64.0.2/24");
    g_assert_cmpstr(data(i, "dns"), ==, "203.0.113.1");
    g_assert_cmpstr(data(i, "allowed-ips"), ==, "0.0.0.0/0");
    g_assert_cmpstr(data(i, "keepalive"), ==, "25");
    g_assert_cmpstr(data(i, "awg-jc"), ==, "4");
    g_assert_cmpstr(data(i, "awg-h1"), ==, "1100000000-1199999999");
    g_assert_cmpstr(data(i, "awg-i1"), ==, "<b 0xc2000000010840><r 32>");
    g_assert_cmpstr(secret(i, "private-key"), ==, KEY_B);
    g_assert_cmpstr(secret(i, "awg-headerprotectionkey"), ==, KEY_A);
    g_assert_cmpuint(i->warnings->len, ==, 0);
}

static void test_amneziawg_rules(void)
{
    g_autoptr(PnvImport) hooks = import_ok("[Interface]\nPrivateKey = " KEY_B "\nPostUp = iptables -A x\nTable = off\n"
                                           "Address = fd00::2/64, 10.8.0.2/24\nAddress = 10.8.0.3/24\n"
                                           "ListenPort = 51820\n[Peer]\nPublicKey = " KEY_A
                                           "\nEndpoint = [2001:db8::1]:51820\n"
                                           "AllowedIPs = 0.0.0.0/1, ::/0\nAllowedIPs = 128.0.0.0/1\n");
    g_autoptr(PnvImport) dns = import_ok("[Interface]\nAddress = 10.8.0.2\nDNS = 10.8.0.1, 2001:db8::1, corp.example\n"
                                         "[Peer]\nPublicKey = " KEY_A "\nEndpoint = h.example:51820\nAllowedIPs = 10.0.0.0/8\n");
    g_autoptr(PnvImport) no_dns = import_ok("[Interface]\nAddress = 10.8.0.2/32\n[Peer]\nPublicKey = " KEY_A
                                            "\nEndpoint = h.example:51820\nAllowedIPs = 0.0.0.0/0\n");
    g_autofree char *w = warnings(hooks), *wd = warnings(dns);

    g_assert_cmpstr(w, ==, "wg-quick:PostUp:0 wg-quick:Table:0 dropped:ListenPort:0 ipv6:Address:0 "
                           "extra-address:Address:2 ipv6:AllowedIPs:1");
    g_assert_cmpstr(data(hooks, "address"), ==, "10.8.0.2/24");
    g_assert_cmpstr(data(hooks, "allowed-ips"), ==, "0.0.0.0/0");
    g_assert_cmpstr(data(hooks, "server"), ==, "2001:db8::1");
    g_assert_cmpstr(data(dns, "dns"), ==, "10.8.0.1");
    g_assert_cmpstr(wd, ==, "ipv6:DNS:1 dns-name:DNS:2");
    g_assert_cmpstr(data(dns, "address"), ==, "10.8.0.2/32");
    g_assert_null(data(no_dns, "dns"));
    g_assert_cmpuint(g_hash_table_size(no_dns->secrets), ==, 0);
    import_fails("[Interface]\nAddress = 10.8.0.2/32\n[Peer]\nPublicKey = " KEY_A "\nEndpoint = h:1\nAllowedIPs = 0.0.0.0/0\n"
                 "[Peer]\nPublicKey = " KEY_B "\nEndpoint = g:1\nAllowedIPs = 10.0.0.0/8\n",
                 PNV_E_IMPORT_PEERS);
    import_fails("[Interface]\nAddress = 10.8.0.2/32\nJc = 70000\n[Peer]\nPublicKey = " KEY_A "\nEndpoint = h:1\n"
                 "AllowedIPs = 0.0.0.0/0\n",
                 PNV_E_OUT_OF_RANGE);
    import_fails("[Interface]\nPrivateKey = a\nPrivateKey = b\n", PNV_E_IMPORT_DUPLICATE);
    import_fails("[Interface]\nnot a key value line\n", PNV_E_IMPORT_BAD_LINE);
    import_fails("[Interface]\nAddress = 10.8.0.2/32\n[Weird]\n", PNV_E_IMPORT_BAD_LINE);
}

static void test_vectors(void)
{
    g_autofree char *huge = g_strnfill(PNV_IMPORT_MAX_SIZE + 1, 'a');
    g_autofree char *long_sni = g_strnfill(4000, 'a');
    g_autofree char *long_link = g_strconcat("hy2://x@h.example:443?sni=", long_sni, NULL);

    /* Newlines and control characters in values, encoded or not. */
    import_fails("hy2://x@h.example:443?sni=a%0Ab.example", PNV_E_CONTROL_CHAR);
    import_fails("hy2://pa%01ss@h.example:443", PNV_E_CONTROL_CHAR);
    import_fails("[Interface]\nAddress = 10.8.0.2/32\nI1 = <b 0x01>\x1b[31m\n[Peer]\nPublicKey = " KEY_A
                 "\nEndpoint = h:1\nAllowedIPs = 0.0.0.0/0\n",
                 PNV_E_CONTROL_CHAR);
    /* An '=' would read as another key in the awg config. */
    import_fails("[Interface]\nAddress = 10.8.0.2/32\nI1 = <b 0x01>=x\n[Peer]\nPublicKey = " KEY_A
                 "\nEndpoint = h:1\nAllowedIPs = 0.0.0.0/0\n",
                 PNV_E_BAD_CHAIN);
    /* An encoded NUL, broken encoding, truncated links. */
    import_fails("hy2://x@h.example:443?sni=a%00b", PNV_E_IMPORT_BAD_LINK);
    import_fails("hy2://x@h.example:443?sni=%zz", PNV_E_IMPORT_BAD_LINK);
    import_fails("hy2://", PNV_E_IMPORT_BAD_LINK);
    import_fails("hy2://x@", PNV_E_IMPORT_BAD_LINK);
    import_fails("vless://u@h.example?type=xhttp&security=reality", PNV_E_IMPORT_BAD_LINK);
    import_fails("hy2://x@h.example:443/some/path", PNV_E_IMPORT_BAD_LINK);
    import_fails("hy2://x@h.example:443?sni=a&sni=b", PNV_E_IMPORT_DUPLICATE);
    /* Paths in extra are refused (vless-details); a parameter named extra is just one the form lacks. */
    {
        g_autoptr(PnvImport) e = import_ok("hy2://x@h.example:443?extra=%7B%22path%22%3A1%7D");
        g_autofree char *w = warnings(e);
        g_assert_cmpstr(w, ==, "dropped:extra:0");
        g_assert_null(data(e, "extra"));
    }
    /* Sizes. */
    import_fails(huge, PNV_E_IMPORT_TOO_LARGE);
    import_fails(long_link, PNV_E_TOO_LONG);
    import_fails("hy2://x@h.example:443?sni=\xff", PNV_E_NOT_UTF8);
    /* Neither format. */
    import_fails("ss://whatever", PNV_E_IMPORT_FORMAT);
    import_fails("", PNV_E_IMPORT_FORMAT);
    g_assert_null(pnv_import_service("trojan://x@h:1"));
}

static void test_codes_named(void)
{
    for (int c = PNV_CODES_PARSER_FIRST; c <= PNV_CODES_PARSER_LAST; c++) {
        g_autofree char *text = NULL;
        PnvProblem p = {c, (char *)"key", (char *)"detail", 1, 0, 2};
        g_assert_cmpstr(pnv_code_name(c), !=, "unknown-code");
        text = pnv_problem_describe(&p);
        g_assert_null(strstr(text, "problem"));
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpint(argc, ==, 2);
    source = argv[1];
    g_test_add_func("/import/bench-hysteria2", test_bench_hysteria2);
    g_test_add_func("/import/hysteria2-details", test_hysteria2_details);
    g_test_add_func("/import/hysteria2-unsupported", test_hysteria2_unsupported);
    g_test_add_func("/import/bench-vless", test_bench_vless);
    g_test_add_func("/import/vless-details", test_vless_details);
    g_test_add_func("/import/bench-amneziawg", test_bench_amneziawg);
    g_test_add_func("/import/amneziawg-rules", test_amneziawg_rules);
    g_test_add_func("/import/vectors", test_vectors);
    g_test_add_func("/import/codes-named", test_codes_named);
    return g_test_run();
}
