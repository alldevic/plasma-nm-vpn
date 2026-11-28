/* SPDX-License-Identifier: MIT */
/* Golden files for the xray VLESS config (4.1): <case>.profile → <case>.json; PNV_GOLDEN_UPDATE=1 rewrites.
 * Usage: test-xray-config <data dir> <golden dir> */
#include <string.h>

#include "config.h"

static const char *data_dir, *golden_dir;

static PnvList *lists(void)
{
    static const char *const files[][2] = {{"extra/", "vless-extra.list"}, {"xhttp-extra/", "xhttp-extra.list"},
                                           {"tls-fingerprint/", "tls-fingerprint.list"},
                                           {"xhttp-mode/", "xhttp-mode.list"}};
    PnvList *all = pnv_list_new();

    for (gsize i = 0; i < G_N_ELEMENTS(files); i++) {
        g_autofree char *path = g_build_filename(data_dir, files[i][1], NULL);
        g_assert_true(pnv_list_merge_file(all, path, files[i][0], NULL));
    }
    return all;
}

static PnvProfile *parse(GHashTable *data, GHashTable *secrets, PnvProblem *p)
{
    g_autoptr(PnvList) all = lists();
    return pnv_profile_parse(PNV_SERVICE_VLESS, data, secrets, all, p);
}

static char *generate(const char *path)
{
    g_autofree char *text = NULL, *ip = NULL, *template_path = g_build_filename(data_dir, "vless.json", NULL);
    g_auto(GStrv) lines = NULL;
    g_autoptr(GHashTable) data = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(GHashTable) secrets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(JsonNode) template = pnv_xray_load_template(template_path, NULL);
    g_auto(PnvProblem) problem = {0};
    g_autoptr(PnvProfile) profile = NULL;
    PnvXrayTarget target = {"pnv-5ca1ab1e", NULL, "eth0"};

    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    lines = g_strsplit(text, "\n", -1);
    for (GStrv l = lines; *l; l++) {
        const char *eq = strchr(*l, '='), *sp = strchr(*l, ' ');
        if (g_str_has_prefix(*l, "ip "))
            ip = g_strdup(*l + 3);
        else if (eq && sp && sp < eq)
            g_hash_table_insert(**l == 'd' ? data : secrets, g_strndup(sp + 1, eq - sp - 1), g_strdup(eq + 1));
    }
    profile = parse(data, secrets, &problem);
    if (!profile)
        g_error("%s: %s", path, pnv_problem_describe(&problem));
    target.server_ip = ip ? ip : profile->server;
    return pnv_xray_vless_config(template, profile, &target, NULL);
}

static void test_golden(void)
{
    g_autoptr(GDir) dir = g_dir_open(golden_dir, 0, NULL);
    gboolean update = g_strcmp0(g_getenv("PNV_GOLDEN_UPDATE"), "1") == 0;
    const char *name;
    guint n = 0;

    while ((name = g_dir_read_name(dir))) {
        g_autofree char *base = NULL, *profile = NULL, *golden = NULL, *got = NULL, *want = NULL;

        if (!g_str_has_suffix(name, ".profile"))
            continue;
        base = g_strndup(name, strlen(name) - strlen(".profile"));
        profile = g_build_filename(golden_dir, name, NULL);
        golden = g_strconcat(golden_dir, "/", base, ".json", NULL);
        got = generate(profile);
        g_assert_nonnull(got);
        if (update) {
            g_autofree char *with_newline = g_strconcat(got, "\n", NULL);
            g_assert_true(g_file_set_contents(golden, with_newline, -1, NULL));
        } else if (!g_file_get_contents(golden, &want, NULL, NULL) || strcmp(g_strchomp(want), got) != 0) {
            g_error("%s: the generated config differs from %s.json", base, base);
        }
        n++;
    }
    g_assert_cmpuint(n, >=, 11);
}

static void test_rejected(void)
{
    static const struct {
        const char *key, *value;
        PnvCode code;
    } cases[] = {
        {"extra", "{\"streamSettings\":{\"sockopt\":{\"interface\":\"lo\"}}}", PNV_E_EXTRA_NOT_ALLOWED},
        {"extra", "{\"streamSettings\":{\"sockopt\":{\"dialerProxy\":\"direct\"}}}", PNV_E_EXTRA_NOT_ALLOWED},
        {"extra", "{\"proxySettings\":{\"tag\":\"direct\"}}", PNV_E_EXTRA_NOT_ALLOWED},
        {"extra", "{\"settings\":{\"vnext\":[]}}", PNV_E_EXTRA_NOT_ALLOWED},
        {"extra", "{\"streamSettings\":{\"realitySettings\":{\"mldsa65Verify\":\"x\",\"masterKeyLog\":\"/tmp/k\"}}}",
         PNV_E_EXTRA_NOT_ALLOWED},
        {"extra", "{\"streamSettings\":{\"sockopt\":{\"tcpFastOpen\":true}},\"tag\":\"x\"}", PNV_E_EXTRA_NOT_ALLOWED},
        {"xhttp-extra", "{\"downloadSettings\":{\"address\":\"203.0.113.9\"}}", PNV_E_EXTRA_NOT_ALLOWED},
        {"xhttp-extra", "{\"headers\":{\"Host\":\"a\\nb\"}}", PNV_E_EXTRA_BAD_VALUE},
        {"xhttp-extra", "{\"headers\":{\"X\":{\"y\":1}}}", PNV_E_EXTRA_NOT_ALLOWED},
        {"xhttp-extra", "{\"path\":\"/other\"}", PNV_E_EXTRA_FORBIDDEN},
        {"tls-fingerprint", "unsafe", PNV_E_BAD_ENUM},
        {"tls-fingerprint", "hellogolang", PNV_E_BAD_ENUM},
        {"xhttp-mode", "stream", PNV_E_BAD_ENUM},
        {"xhttp-path", "no-slash", PNV_E_BAD_PATH},
        {"xhttp-path", "/a b", PNV_E_BAD_PATH},
        {"tls-sni", "www.stand.test\nx", PNV_E_CONTROL_CHAR},
        {"reality-pbk", "OkmsNCxt-wKj42ChHHmk1l-F1XTIG5qE7KoKeOtO0U", PNV_E_BAD_LENGTH},
        {"reality-sid", "abc", PNV_E_BAD_HEX},
        {"encryption", "aes-128-gcm", PNV_E_BAD_ENCRYPTION},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_autoptr(GHashTable) data = g_hash_table_new(g_str_hash, g_str_equal);
        g_autoptr(GHashTable) secrets = g_hash_table_new(g_str_hash, g_str_equal);
        g_auto(PnvProblem) p = {0};
        g_autoptr(PnvProfile) profile = NULL;

        g_hash_table_insert(data, "server", "198.51.100.1");
        g_hash_table_insert(data, "port", "443");
        g_hash_table_insert(data, "reality-pbk", "OkmsNCxt-wKj42ChHHmk1l-F1XTIG5qE7KoKeOtO0U4");
        g_hash_table_insert(data, "tls-sni", "www.stand.test");
        g_hash_table_insert(secrets, "uuid", "3c2b9a7e-5f1d-4e8a-9b6c-0d7e8f9a1b2c");
        g_hash_table_insert(data, (gpointer)cases[i].key, (gpointer)cases[i].value);
        profile = parse(data, secrets, &p);
        if (profile || p.code != cases[i].code)
            g_error("case %zu (%s=%s): got %s, want %s", i, cases[i].key, cases[i].value,
                    profile ? "accepted" : pnv_code_name(p.code), pnv_code_name(cases[i].code));
    }
}

/* A value added to the list file is accepted without rebuilding. */
static void test_list_is_data(void)
{
    g_autoptr(PnvList) all = lists();
    g_autoptr(PnvList) added = pnv_list_parse("hellogolang\n", "test", NULL);
    g_autoptr(GHashTable) data = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(GHashTable) secrets = g_hash_table_new(g_str_hash, g_str_equal);
    g_auto(PnvProblem) p = {0};
    g_autoptr(PnvProfile) profile = NULL;

    pnv_list_merge(all, added, "tls-fingerprint/");
    g_hash_table_insert(data, "server", "198.51.100.1");
    g_hash_table_insert(data, "port", "443");
    g_hash_table_insert(data, "reality-pbk", "OkmsNCxt-wKj42ChHHmk1l-F1XTIG5qE7KoKeOtO0U4");
    g_hash_table_insert(data, "tls-sni", "www.stand.test");
    g_hash_table_insert(data, "tls-fingerprint", "hellogolang");
    g_hash_table_insert(secrets, "uuid", "3c2b9a7e-5f1d-4e8a-9b6c-0d7e8f9a1b2c");
    profile = pnv_profile_parse(PNV_SERVICE_VLESS, data, secrets, all, &p);
    g_assert_nonnull(profile);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpint(argc, ==, 3);
    data_dir = argv[1];
    golden_dir = argv[2];
    g_test_add_func("/xray/golden", test_golden);
    g_test_add_func("/xray/rejected", test_rejected);
    g_test_add_func("/xray/list-is-data", test_list_is_data);
    return g_test_run();
}
