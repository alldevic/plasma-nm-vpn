/* SPDX-License-Identifier: MIT */
/* Golden files for the awg setconf config (3.1): <case>.profile → <case>.conf; PNV_GOLDEN_UPDATE=1 rewrites.
 * Usage: test-awg-config <amneziawg-params.list> <golden dir> */
#include <string.h>

#include "config.h"

static const char *list_path, *golden_dir;

static char *generate(const char *path)
{
    g_autofree char *text = NULL, *ip = NULL;
    g_auto(GStrv) lines = NULL;
    g_autoptr(GHashTable) data = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(GHashTable) secrets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(PnvList) allowed = pnv_list_new();

    g_assert_true(pnv_list_merge_file(allowed, list_path, "awg/", NULL));
    g_auto(PnvProblem) problem = {0};
    g_autoptr(PnvProfile) profile = NULL;

    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    lines = g_strsplit(text, "\n", -1);
    for (GStrv l = lines; *l; l++) {
        const char *eq = strchr(*l, '='), *sp = strchr(*l, ' ');
        if (g_str_has_prefix(*l, "ip "))
            ip = g_strdup(*l + 3);
        else if (eq && sp && sp < eq)
            g_hash_table_insert(**l == 'd' ? data : secrets, g_strndup(sp + 1, eq - sp - 1), g_strdup(eq + 1));
    }
    profile = pnv_profile_parse(PNV_SERVICE_AMNEZIAWG, data, secrets, allowed, &problem);
    if (!profile)
        g_error("%s: %s", path, pnv_problem_describe(&problem));
    return pnv_awg_config(profile, ip ? ip : profile->server);
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
        golden = g_strconcat(golden_dir, "/", base, ".conf", NULL);
        got = generate(profile);
        if (update)
            g_assert_true(g_file_set_contents(golden, got, -1, NULL));
        else if (!g_file_get_contents(golden, &want, NULL, NULL) || strcmp(want, got) != 0)
            g_error("%s: the generated config differs from %s.conf", base, base);
        n++;
    }
    g_assert_cmpuint(n, >=, 3);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpint(argc, ==, 3);
    list_path = argv[1];
    golden_dir = argv[2];
    g_test_add_func("/awg/golden", test_golden);
    return g_test_run();
}
