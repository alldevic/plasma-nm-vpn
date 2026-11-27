/* SPDX-License-Identifier: MIT */
/* The AmneziaWG schema (3.1): fields, secrets and the allowlisted awg-* parameters.
 * Usage: test-awg-profile <amneziawg-params.list> */
#include <string.h>

#include "pnv-profile.h"

static const char *list_path;

#define KEY_A "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXphYmNkZWY="
#define KEY_B "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDE="

static PnvProfile *parse(const char *extra_key, const char *extra_value, gboolean secret, PnvProblem *p)
{
    g_autoptr(GHashTable) data = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(GHashTable) secrets = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(PnvList) allowed = pnv_list_new();

    g_assert_true(pnv_list_merge_file(allowed, list_path, "awg/", NULL));

    g_assert_nonnull(allowed);
    g_hash_table_insert(data, "server", "198.51.100.1");
    g_hash_table_insert(data, "port", "51820");
    g_hash_table_insert(data, "address", "100.64.0.2/24");
    g_hash_table_insert(data, "peer-public-key", KEY_A);
    g_hash_table_insert(data, "allowed-ips", "0.0.0.0/1, 128.0.0.0/1");
    g_hash_table_insert(data, "awg-jc", "4");
    g_hash_table_insert(data, "awg-h1", "1100000000-1199999999");
    g_hash_table_insert(data, "awg-i1", "<b 0xc2000000010840><r 32>");
    g_hash_table_insert(secrets, "private-key", KEY_B);
    if (extra_key)
        g_hash_table_insert(secret ? secrets : data, (gpointer)extra_key, (gpointer)extra_value);
    return pnv_profile_parse(PNV_SERVICE_AMNEZIAWG, data, secrets, allowed, p);
}

static void test_accepted(void)
{
    g_auto(PnvProblem) p = {0};
    g_autoptr(PnvProfile) profile = parse("awg-headerprotectionkey", KEY_A, TRUE, &p);
    const PnvAmneziawg *a;

    g_assert_nonnull(profile);
    a = &profile->amneziawg;
    g_assert_cmpuint(a->port, ==, 51820);
    g_assert_cmpuint(g_array_index(a->address, PnvCidr4, 0).prefix, ==, 24);
    g_assert_cmpuint(a->allowed_ips->len, ==, 1);
    g_assert_cmpuint(g_array_index(a->allowed_ips, PnvCidr4, 0).prefix, ==, 0);
    g_assert_cmpstr(a->private_key, ==, KEY_B);
    g_assert_cmpstr(a->header_protection_key, ==, KEY_A);
    g_assert_null(a->preshared_key);
    g_assert_cmpuint(a->params->len, ==, 3);
    g_assert_cmpstr(((PnvAwgParam *)a->params->pdata[0])->key, ==, "H1");
    g_assert_cmpstr(((PnvAwgParam *)a->params->pdata[2])->key, ==, "Jc");
}

static void test_rejected(void)
{
    static const struct {
        const char *key, *value;
        gboolean secret;
        PnvCode code;
        const char *problem_key;
    } cases[] = {
        {"awg-jc", "4\nPostUp = rm -rf /", FALSE, PNV_E_CONTROL_CHAR, "awg-jc"},
        {"awg-i1", "<b 0x01>\n[Peer]", FALSE, PNV_E_CONTROL_CHAR, "awg-i1"},
        {"awg-postup", "x", FALSE, PNV_E_UNKNOWN_KEY, "awg-postup"},
        {"awg-headerprotectionkey", KEY_A, FALSE, PNV_E_UNKNOWN_KEY, "awg-headerprotectionkey"},
        {"awg-jc", "70000", FALSE, PNV_E_OUT_OF_RANGE, "awg-jc"},
        {"awg-h2", "20-10", FALSE, PNV_E_BAD_PORT_RANGE, "awg-h2"},
        {"awg-h2", "1-2-3", FALSE, PNV_E_BAD_NUMBER, "awg-h2"},
        {"awg-randomtrailers", "yes", FALSE, PNV_E_BAD_ENUM, "awg-randomtrailers"},
        {"peer-public-key", "short", FALSE, PNV_E_BAD_LENGTH, "peer-public-key"},
        {"preshared-key", "not base64!", TRUE, PNV_E_BAD_BASE64, "preshared-key"},
        {"port", "0", FALSE, PNV_E_BAD_PORT, "port"},
        {"allowed-ips", "10.0.0.0/33", FALSE, PNV_E_BAD_CIDR, "allowed-ips"},
        {"private-key", "", TRUE, PNV_E_MISSING, "private-key"},
        {"ports", "443", FALSE, PNV_E_UNKNOWN_KEY, "ports"},
        {"allowed-ips", "::/0", FALSE, PNV_E_MISSING, "allowed-ips"},
        {"address", "fd00::2/64", FALSE, PNV_E_MISSING, "address"},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_auto(PnvProblem) p = {0};
        g_autoptr(PnvProfile) profile = parse(cases[i].key, cases[i].value, cases[i].secret, &p);

        if (profile)
            g_error("case %zu (%s) accepted", i, cases[i].key);
        if (p.code != cases[i].code || g_strcmp0(p.key, cases[i].problem_key) != 0)
            g_error("case %zu: got %s %s, want %s %s", i, pnv_code_name(p.code), p.key,
                    pnv_code_name(cases[i].code), cases[i].problem_key);
    }
}

static void test_ipv6_skipped(void)
{
    g_auto(PnvProblem) p = {0};
    g_autoptr(PnvProfile) profile = parse("allowed-ips", "0.0.0.0/0, ::/0", FALSE, &p);

    g_assert_nonnull(profile);
    g_assert_cmpuint(profile->amneziawg.skipped_ipv6, ==, 1);
    g_assert_cmpuint(profile->amneziawg.allowed_ips->len, ==, 1);
}

static void test_without_allowlist(void)
{
    g_autoptr(GHashTable) data = g_hash_table_new(g_str_hash, g_str_equal);
    g_auto(PnvProblem) p = {0};

    g_hash_table_insert(data, "awg-jc", "4");
    g_assert_null(pnv_profile_parse(PNV_SERVICE_AMNEZIAWG, data, NULL, NULL, &p));
    g_assert_cmpint(p.code, ==, PNV_E_UNKNOWN_KEY);
}

static void test_secrets(void)
{
    const PnvSecret *s = pnv_service_secrets(PNV_SERVICE_AMNEZIAWG);

    g_assert_cmpstr(s[0].key, ==, "private-key");
    g_assert_false(s[0].optional);
    g_assert_true(s[1].optional && s[2].optional);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpint(argc, ==, 2);
    list_path = argv[1];
    g_test_add_func("/awg/accepted", test_accepted);
    g_test_add_func("/awg/rejected", test_rejected);
    g_test_add_func("/awg/ipv6-skipped", test_ipv6_skipped);
    g_test_add_func("/awg/without-allowlist", test_without_allowlist);
    g_test_add_func("/awg/secrets", test_secrets);
    return g_test_run();
}
