/* SPDX-License-Identifier: MIT */
/* Export (6.8): for each format, importing (6.6) the export gives back every field the format carries, with and
 * without secrets; what it cannot carry is reported. Usage: test-export <source dir> */
#include <string.h>

#include "pnv-profile.h"

static const char *source;

#define KEY_A "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWY="
#define KEY_B "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDE="
#define UUID "a3f1c2d4-5b6e-4f70-8a9b-0c1d2e3f4a59"

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

static GHashTable *table(const char *const *pairs)
{
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (; pairs[0]; pairs += 2)
        g_hash_table_insert(t, g_strdup(pairs[0]), g_strdup(pairs[1]));
    return t;
}

static void clear_warning(gpointer w)
{
    pnv_problem_clear(w);
}

static char *names(GArray *warnings)
{
    GString *out = g_string_new(NULL);
    for (guint i = 0; i < warnings->len; i++) {
        const PnvProblem *w = &g_array_index(warnings, PnvProblem, i);
        g_assert_true(w->code >= PNV_CODES_EXPORT_FIRST && w->code <= PNV_CODES_EXPORT_LAST);
        g_string_append_printf(out, "%s%s:%s", i ? " " : "", pnv_code_name(w->code), w->key);
    }
    return g_string_free(out, FALSE);
}

/* Export, import, and compare the carried keys; returns the export warnings. */
static char *round_trip(const char *service, const char *const *data_pairs, const char *const *secret_pairs,
                        gboolean with_secrets, const char *const *carried, char **text_out)
{
    g_autoptr(GHashTable) data = table(data_pairs), secrets = table(secret_pairs);
    g_autoptr(PnvList) lists = lists_for(service);
    g_autoptr(GArray) warnings = g_array_new(FALSE, TRUE, sizeof(PnvProblem));
    g_auto(PnvProblem) p = {0};
    g_autofree char *text = NULL;
    g_autoptr(PnvImport) back = NULL;

    g_array_set_clear_func(warnings, clear_warning);
    text = pnv_export(service, data, with_secrets ? secrets : NULL, lists, warnings);
    g_assert_nonnull(text);
    back = pnv_import(text, lists, &p);
    if (!back)
        g_error("re-import of %s failed: %s (%s)", text, pnv_code_name(p.code), p.key ? p.key : "");
    g_assert_cmpstr(back->service, ==, service);
    for (guint i = 0; carried[i]; i++)
        g_assert_cmpstr(g_hash_table_lookup(back->data, carried[i]), ==, g_hash_table_lookup(data, carried[i]));
    if (with_secrets) {
        GHashTableIter it;
        gpointer key, value;
        g_hash_table_iter_init(&it, secrets);
        while (g_hash_table_iter_next(&it, &key, &value))
            g_assert_cmpstr(g_hash_table_lookup(back->secrets, key), ==, value);
    } else {
        g_assert_cmpuint(g_hash_table_size(back->secrets), ==, 0);
    }
    if (text_out)
        *text_out = g_steal_pointer(&text);
    return names(warnings);
}

static void test_hysteria2(void)
{
    const char *const data[] = {"schema", "1", "server", "srv.stand.test", "ports", "443,20000-20100",
                                "obfs", "salamander", "tls-sni", "sni.example", "tls-insecure", "yes",
                                "tls-alpn", "h3", "hop-interval", "30", "kill-switch", "yes", "allow-lan", "yes",
                                "dns", "203.0.113.1", "password-flags", "0", NULL};
    const char *const secrets[] = {"password", "p@ss/w:rd&x=1", "obfs-password", "o b+s", NULL};
    const char *const carried[] = {"server", "ports", "obfs", "tls-sni", "tls-insecure", NULL};
    g_autofree char *text = NULL, *plain = NULL;
    g_autofree char *lost = round_trip(PNV_SERVICE_HYSTERIA2, data, secrets, TRUE, carried, &text);
    g_autofree char *lost2 = round_trip(PNV_SERVICE_HYSTERIA2, data, secrets, FALSE, carried, &plain);

    g_assert_cmpstr(lost, ==, "export-lost:dns export-lost:hop-interval export-lost:kill-switch export-lost:tls-alpn");
    g_assert_cmpstr(lost2, ==, lost);
    g_assert_null(strstr(plain, "p%40ss") );
    g_assert_null(strstr(plain, "obfs-password"));
}

static void test_ipv6_server(void)
{
    const char *const data[] = {"server", "2001:db8::1", "ports", "443", NULL};
    const char *const none[] = {NULL};
    const char *const carried[] = {"server", "ports", NULL};
    g_autofree char *text = NULL;
    g_autofree char *lost = round_trip(PNV_SERVICE_HYSTERIA2, data, none, TRUE, carried, &text);
    g_assert_nonnull(strstr(text, "[2001:db8::1]:443"));
}

static void test_vless(void)
{
    g_autofree char *golden_path = g_build_filename(source, "tests/golden/xray-26.11.9/mlkem.json", NULL);
    g_autofree char *golden = NULL, *mlkem = NULL, *text = NULL, *plain = NULL;
    const char *start;

    g_assert_true(g_file_get_contents(golden_path, &golden, NULL, NULL));
    start = strstr(golden, "mlkem768x25519plus.");
    mlkem = g_strndup(start, strchr(start, '"') - start);
    {
        const char *const data[] = {"server", "198.51.100.1", "port", "443", "encryption", mlkem,
                                    "reality-pbk", "6lgzU9D_m5zHBBWGxPuMXCYjkmwrlbWLh4LyUOoOdXA",
                                    "reality-sid", "b229853aa7dfca4f", "reality-spx", "/s&p=x",
                                    "tls-fingerprint", "firefox", "tls-sni", "www.stand.test",
                                    "xhttp-path", "/3042d8a1?q=1", "xhttp-host", "cdn.stand.test",
                                    "xhttp-mode", "packet-up", "xhttp-extra", "{\"xPaddingBytes\":\"100-1000\"}",
                                    "extra", "{\"mux\":{\"enabled\":false}}", "allow-lan", "no", "mtu", "1400",
                                    "uuid-flags", "1", NULL};
        const char *const secrets[] = {"uuid", UUID, NULL};
        const char *const carried[] = {"server", "port", "encryption", "reality-pbk", "reality-sid", "reality-spx",
                                       "tls-fingerprint", "tls-sni", "xhttp-path", "xhttp-host", "xhttp-mode",
                                       "xhttp-extra", NULL};
        g_autofree char *lost = round_trip(PNV_SERVICE_VLESS, data, secrets, TRUE, carried, &text);
        g_autofree char *lost2 = round_trip(PNV_SERVICE_VLESS, data, secrets, FALSE, carried, &plain);

        g_assert_cmpstr(lost, ==, "export-lost:allow-lan export-lost:extra export-lost:mtu");
        g_assert_cmpstr(lost2, ==, lost);
        g_assert_nonnull(strstr(text, mlkem));
        g_assert_null(strstr(plain, UUID));
    }
}

static void test_amneziawg(void)
{
    const char *const data[] = {"server", "198.51.100.1", "port", "51820", "address", "100.64.0.2/24",
                                "dns", "203.0.113.1, 1.1.1.1", "mtu", "1420", "peer-public-key", KEY_A,
                                "allowed-ips", "0.0.0.0/0", "keepalive", "25", "awg-jc", "4",
                                "awg-h1", "1100000000-1199999999", "awg-i1", "<b 0xc2000000010840><r 32>",
                                "kill-switch", "yes", "allow-lan", "no", "private-key-flags", "0",
                                "preshared-key-flags", "4", NULL};
    const char *const secrets[] = {"private-key", KEY_B, "awg-headerprotectionkey", KEY_A, NULL};
    const char *const carried[] = {"server", "port", "address", "dns", "mtu", "peer-public-key", "allowed-ips",
                                   "keepalive", "awg-jc", "awg-h1", "awg-i1", NULL};
    g_autofree char *text = NULL, *plain = NULL;
    g_autofree char *lost = round_trip(PNV_SERVICE_AMNEZIAWG, data, secrets, TRUE, carried, &text);
    g_autofree char *lost2 = round_trip(PNV_SERVICE_AMNEZIAWG, data, secrets, FALSE, carried, &plain);

    g_assert_cmpstr(lost, ==, "export-lost:allow-lan export-lost:kill-switch");
    g_assert_nonnull(strstr(text, "Jc = 4\n"));
    g_assert_null(strstr(plain, "PrivateKey"));
    g_assert_null(strstr(plain, "HeaderProtectionKey"));
    g_assert_null(strstr(text, "PresharedKey"));
    /* No dns, no DNS line: the import leaves dns empty. */
    {
        const char *const bare[] = {"server", "h.example", "port", "51820", "address", "10.8.0.2/32",
                                    "peer-public-key", KEY_A, "allowed-ips", "10.0.0.0/8", NULL};
        const char *const none[] = {NULL};
        const char *const kept[] = {"dns", NULL};
        g_autofree char *conf = NULL;
        g_autofree char *l = round_trip(PNV_SERVICE_AMNEZIAWG, bare, none, TRUE, kept, &conf);
        g_assert_null(strstr(conf, "DNS"));
    }
}

static void test_asked_secrets(void)
{
    const char *const data_pairs[] = {"server", "h.example", "ports", "443", "password-flags", "2",
                                      "obfs", "salamander", "obfs-password-flags", "4", NULL};
    const char *const none[] = {NULL};
    g_autoptr(GHashTable) data = table(data_pairs), secrets = table(none);
    g_autoptr(GArray) warnings = g_array_new(FALSE, TRUE, sizeof(PnvProblem));
    g_autofree char *text = NULL, *w = NULL;

    g_array_set_clear_func(warnings, clear_warning);
    text = pnv_export(PNV_SERVICE_HYSTERIA2, data, secrets, NULL, warnings);
    w = names(warnings);
    /* Asked every time: reported; not required and empty: simply left out. */
    g_assert_cmpstr(w, ==, "export-no-secret:password");
    g_assert_null(pnv_export("org.example.unknown", data, NULL, NULL, NULL));
}

static void test_codes_named(void)
{
    for (int c = PNV_CODES_EXPORT_FIRST; c <= PNV_CODES_EXPORT_LAST; c++)
        g_assert_cmpstr(pnv_code_name(c), !=, "unknown-code");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpint(argc, ==, 2);
    source = argv[1];
    g_test_add_func("/export/hysteria2", test_hysteria2);
    g_test_add_func("/export/ipv6-server", test_ipv6_server);
    g_test_add_func("/export/vless", test_vless);
    g_test_add_func("/export/amneziawg", test_amneziawg);
    g_test_add_func("/export/asked-secrets", test_asked_secrets);
    g_test_add_func("/export/codes-named", test_codes_named);
    return g_test_run();
}
