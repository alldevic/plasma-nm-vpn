/* SPDX-License-Identifier: MIT */
/* Golden files for the Hysteria2 sing-box config (2.5): every <case>.profile in the golden directory of the
 * pinned sing-box version must generate exactly <case>.json. PNV_GOLDEN_UPDATE=1 rewrites them instead.
 * Usage: test-sb-config <template> <extra allowlist> <golden dir> */
#include <string.h>

#include "config.h"

static const char *template_path, *allowlist_path, *golden_dir;

/* "data key=value", "secret key=value" and "ip <server IP>" lines. */
static PnvProfile *load_profile(const char *path, char **ip, PnvProblem *problem)
{
    g_autofree char *text = NULL;
    g_auto(GStrv) lines = NULL;
    g_autoptr(GHashTable) data = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(GHashTable) secrets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(PnvList) allowed = pnv_list_new();

    g_assert_true(pnv_list_merge_file(allowed, allowlist_path, "extra/", NULL));

    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    lines = g_strsplit(text, "\n", -1);
    for (GStrv l = lines; *l; l++) {
        const char *eq = strchr(*l, '=');
        if (g_str_has_prefix(*l, "ip "))
            *ip = g_strdup(*l + 3);
        else if (eq && (g_str_has_prefix(*l, "data ") || g_str_has_prefix(*l, "secret ")))
            g_hash_table_insert(**l == 'd' ? data : secrets, g_strndup(strchr(*l, ' ') + 1, eq - strchr(*l, ' ') - 1),
                                g_strdup(eq + 1));
    }
    g_assert_nonnull(allowed);
    return pnv_profile_parse(PNV_SERVICE_HYSTERIA2, data, secrets, allowed, problem);
}

static char *generate(const char *profile_path, GError **error)
{
    g_autoptr(JsonNode) template = pnv_sb_load_template(template_path, NULL);
    g_auto(PnvProblem) problem = {0};
    g_autofree char *ip = NULL;
    g_autoptr(PnvProfile) profile = load_profile(profile_path, &ip, &problem);
    PnvSbTarget target = {"pnv-5ca1ab1e", "198.18.0.1/30", 1400, NULL, "eth0"};

    if (!profile)
        g_error("%s: %s", profile_path, pnv_problem_describe(&problem));
    target.server_ip = ip ? ip : profile->server;
    if (profile->mtu)
        target.mtu = profile->mtu;
    return pnv_sb_hysteria2_config(template, profile, &target, error);
}

static void test_golden(void)
{
    g_autoptr(GDir) dir = g_dir_open(golden_dir, 0, NULL);
    gboolean update = g_strcmp0(g_getenv("PNV_GOLDEN_UPDATE"), "1") == 0;
    const char *name;
    guint n = 0;

    g_assert_nonnull(dir);
    while ((name = g_dir_read_name(dir))) {
        g_autofree char *base = NULL, *profile = NULL, *golden = NULL, *got = NULL, *want = NULL;

        if (!g_str_has_suffix(name, ".profile"))
            continue;
        base = g_strndup(name, strlen(name) - strlen(".profile"));
        profile = g_build_filename(golden_dir, name, NULL);
        golden = g_strconcat(golden_dir, "/", base, ".json", NULL);
        got = generate(profile, NULL);
        g_assert_nonnull(got);
        if (update) {
            g_autofree char *with_newline = g_strconcat(got, "\n", NULL);
            g_assert_true(g_file_set_contents(golden, with_newline, -1, NULL));
        } else if (!g_file_get_contents(golden, &want, NULL, NULL) || strcmp(g_strchomp(want), got) != 0) {
            g_error("%s: the generated config differs from %s.json; PNV_GOLDEN_UPDATE=1 rewrites it after review",
                    base, base);
        }
        n++;
    }
    g_assert_cmpuint(n, >=, 9);
}

static void test_rejected(void)
{
    g_autoptr(JsonNode) template = pnv_sb_load_template(template_path, NULL);
    g_autoptr(PnvList) allowed = pnv_list_new();

    g_assert_true(pnv_list_merge_file(allowed, allowlist_path, "extra/", NULL));
    g_autoptr(GHashTable) data = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(GHashTable) secrets = g_hash_table_new(g_str_hash, g_str_equal);
    PnvSbTarget target = {"pnv-5ca1ab1e", "198.18.0.1/30", 1400, "198.51.100.1", NULL};
    g_auto(PnvProblem) problem = {0};
    g_autoptr(GError) error = NULL;
    g_autoptr(PnvProfile) pinned = NULL;
    g_autofree char *config = NULL;
    const char *const forbidden[] = {"{\"detour\":\"direct\"}", "{\"tls\":{\"certificate_path\":\"/etc/shadow\"}}",
                                     "{\"server\":\"203.0.113.9\"}", "{\"bind_interface\":\"lo\"}", NULL};

    g_hash_table_insert(data, "server", "198.51.100.1");
    g_hash_table_insert(data, "ports", "443");
    g_hash_table_insert(secrets, "password", "p");
    for (const char *const *f = forbidden; *f; f++) {
        g_autoptr(PnvProfile) p = NULL;
        g_hash_table_insert(data, "extra", (gpointer)*f);
        p = pnv_profile_parse(PNV_SERVICE_HYSTERIA2, data, secrets, allowed, &problem);
        if (p)
            g_error("extra %s accepted", *f);
    }
    g_hash_table_remove(data, "extra");
    g_hash_table_insert(data, "tls-pin-sha256",
                        "ba884517a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a0e3b5d1e5a1f3d7a0");
    pinned = pnv_profile_parse(PNV_SERVICE_HYSTERIA2, data, secrets, allowed, &problem);
    g_assert_nonnull(pinned);
    config = pnv_sb_hysteria2_config(template, pinned, &target, &error);
    g_assert_null(config);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (argc != 4)
        g_error("usage: test-sb-config <template> <extra allowlist> <golden dir>");
    template_path = argv[1];
    allowlist_path = argv[2];
    golden_dir = argv[3];
    g_test_add_func("/sb/golden", test_golden);
    g_test_add_func("/sb/rejected", test_rejected);
    return g_test_run();
}
