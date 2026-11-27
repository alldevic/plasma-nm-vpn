/* SPDX-License-Identifier: MIT */
/* vpn.data + vpn.secrets → profile or problem. Input: records separated by NUL, each 'd' (data) or 's' (secret),
 * then key=value. */
#include <string.h>

#include "pnv-profile.h"

int LLVMFuzzerTestOneInput(const guint8 *bytes, size_t size);

int LLVMFuzzerTestOneInput(const guint8 *bytes, size_t size)
{
    g_autoptr(GHashTable) data = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(GHashTable) secrets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(PnvList) allowed = pnv_list_parse("up_mbps\ndown_mbps\ntls\ntls.fragment\n", "fuzz", NULL);
    const char *end = (const char *)bytes + size;
    g_auto(PnvProblem) problem = {0};
    g_autoptr(PnvProfile) profile = NULL;

    for (const char *r = (const char *)bytes; r < end;) {
        gsize n = strnlen(r, end - r);
        g_autofree char *record = g_strndup(r, n);
        const char *eq = strchr(record, '=');

        r += n + 1;
        if (!eq || (record[0] != 'd' && record[0] != 's'))
            continue;
        g_hash_table_replace(record[0] == 'd' ? data : secrets, g_strndup(record + 1, eq - record - 1),
                             g_strdup(eq + 1));
    }
    profile = pnv_profile_parse(PNV_SERVICE_HYSTERIA2, data, secrets, allowed, &problem);
    if (!profile) {
        g_autofree char *reason = pnv_problem_describe(&problem);
        g_assert(!strchr(reason, '\n'));
    }
    return 0;
}
