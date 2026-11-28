/* SPDX-License-Identifier: MIT */
/* Our nftables chains in inet plasma_nm_vpn, named after the profile UUID; applied with nft -f from the
 * templates in /usr/share/plasma-nm-vpn/. Only the UUID, which the daemon has checked, is substituted. */
#include <string.h>

#include "pnv-daemon.h"

#define NFT "/usr/sbin/nft"

gboolean pnv_nft_run(const char *script, GError **error)
{
    g_autoptr(GSubprocess) nft = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                                      G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                                  error, NFT, "-f", "-", NULL);
    g_autofree char *err = NULL;

    if (!nft || !g_subprocess_communicate_utf8(nft, script, NULL, NULL, &err, error))
        return FALSE;
    if (!g_subprocess_get_successful(nft)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "nft: %s", err ? g_strstrip(err) : "failed");
        return FALSE;
    }
    return TRUE;
}

gboolean pnv_guard_install(const char *uuid, GError **error)
{
    g_autofree char *template = NULL;
    g_auto(GStrv) parts = NULL;
    g_autofree char *script = NULL;

    g_return_val_if_fail(pnv_uuid_valid(uuid), FALSE);
    if (!g_file_get_contents(PNV_DATA_DIR "/guard.nft", &template, NULL, error))
        return FALSE;
    parts = g_strsplit(template, "@UUID@", -1);
    script = g_strjoinv(uuid, parts);
    return pnv_nft_run(script, error);
}

/* Idempotent: destroy, unlike delete, does not fail on a chain that is not there. */
void pnv_guard_remove(const char *uuid)
{
    g_autofree char *script = NULL;
    g_autoptr(GError) error = NULL;

    g_return_if_fail(pnv_uuid_valid(uuid));
    script = g_strdup_printf("add table inet plasma_nm_vpn\n"
                             "destroy chain inet plasma_nm_vpn guard-out-%s\n"
                             "destroy chain inet plasma_nm_vpn guard-fwd-%s\n",
                             uuid, uuid);
    if (!pnv_nft_run(script, &error))
        g_warning("[%s] cannot remove the DNS and IPv6 guard: %s", uuid, error->message);
}

/* allow-lan=yes: RFC 1918 and link-local stay reachable (DNS to them is already rejected above). The line is the
 * daemon's own constant; "@LAN@<chain>" in the template becomes these rules for that chain, or nothing. */
static char *lan_rules(const char *chain)
{
    return g_strdup_printf("add rule inet plasma_nm_vpn %s ip daddr { 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, "
                           "169.254.0.0/16 } return\n"
                           "add rule inet plasma_nm_vpn %s ip6 daddr fe80::/10 return",
                           chain, chain);
}

static gboolean block_install(const char *prefix, const char *uuid, gboolean allow_lan, GError **error);

gboolean pnv_killswitch_install(const char *uuid, gboolean allow_lan, GError **error)
{
    return block_install("ks-", uuid, allow_lan, error);
}

/* The same rules under other names, for the moment the VPN steps aside (5.5); removed by the daemon, orphan
 * cleanup and --daemon-gone whatever the markers say. */
gboolean pnv_temp_block_install(const char *uuid, gboolean allow_lan, GError **error)
{
    return block_install("tb-", uuid, allow_lan, error);
}

void pnv_temp_block_remove(const char *uuid)
{
    g_autofree char *script = g_strdup_printf("add table inet plasma_nm_vpn\n"
                                              "destroy chain inet plasma_nm_vpn tb-out-%s\n"
                                              "destroy chain inet plasma_nm_vpn tb-fwd-%s\n",
                                              uuid, uuid);
    pnv_nft_run(script, NULL);
}

static gboolean block_install(const char *prefix, const char *uuid, gboolean allow_lan, GError **error)
{
    g_autofree char *template = NULL;
    g_auto(GStrv) lines = NULL;
    GString *script = g_string_new(NULL);
    gboolean ok;

    g_return_val_if_fail(pnv_uuid_valid(uuid), FALSE);
    if (!g_file_get_contents(PNV_DATA_DIR "/killswitch.nft", &template, NULL, error)) {
        g_string_free(script, TRUE);
        return FALSE;
    }
    lines = g_strsplit(template, "\n", -1);
    for (GStrv l = lines; *l; l++) {
        g_auto(GStrv) parts = g_strsplit(*l, "@UUID@", -1);
        g_autofree char *joined = g_strjoinv(uuid, parts);
        g_autofree char *out_name = g_strconcat(prefix, "out-", NULL), *fwd_name = g_strconcat(prefix, "fwd-", NULL);
        g_auto(GStrv) outs = g_strsplit(joined, "ks-out-", -1);
        g_autofree char *renamed = g_strjoinv(out_name, outs);
        g_auto(GStrv) fwds = g_strsplit(renamed, "ks-fwd-", -1);
        g_autofree char *line = g_strjoinv(fwd_name, fwds);

        if (g_str_has_prefix(line, "@LAN@")) {
            g_autofree char *rules = allow_lan ? lan_rules(line + strlen("@LAN@")) : NULL;
            if (rules)
                g_string_append_printf(script, "%s\n", rules);
            continue;
        }
        g_string_append_printf(script, "%s\n", line);
    }
    ok = pnv_nft_run(script->str, error);
    g_string_free(script, TRUE);
    return ok;
}

void pnv_killswitch_remove(const char *uuid)
{
    g_autofree char *script = NULL;
    g_autoptr(GError) error = NULL;

    g_return_if_fail(pnv_uuid_valid(uuid));
    script = g_strdup_printf("add table inet plasma_nm_vpn\n"
                             "destroy chain inet plasma_nm_vpn ks-out-%s\n"
                             "destroy chain inet plasma_nm_vpn ks-fwd-%s\n",
                             uuid, uuid);
    if (!pnv_nft_run(script, &error))
        g_warning("[%s] cannot remove the kill switch: %s", uuid, error->message);
}

/* Whether this profile's kill switch chains exist (left over by an earlier session, 5.2). */
gboolean pnv_killswitch_present(const char *uuid)
{
    g_autofree char *chain = g_strdup_printf("ks-out-%s", uuid);
    g_autoptr(GSubprocess) nft = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                                                  NULL, NFT, "list", "chain", "inet", "plasma_nm_vpn", chain, NULL);

    return nft && g_subprocess_wait(nft, NULL, NULL) && g_subprocess_get_successful(nft);
}

GStrv pnv_killswitch_list(void)
{
    g_autoptr(GSubprocess) nft = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                                                  NULL, NFT, "list", "table", "inet", "plasma_nm_vpn", NULL);
    g_autofree char *out = NULL;
    g_autoptr(GStrvBuilder) uuids = g_strv_builder_new();
    g_auto(GStrv) lines = NULL;

    if (nft && g_subprocess_communicate_utf8(nft, NULL, NULL, &out, NULL, NULL) && out) {
        lines = g_strsplit(out, "\n", -1);
        for (GStrv l = lines; *l; l++) {
            const char *at = strstr(*l, "chain ks-out-");
            g_autofree char *uuid = at ? g_strndup(at + strlen("chain ks-out-"), 36) : NULL;
            if (uuid && pnv_uuid_valid(uuid))
                g_strv_builder_add(uuids, uuid);
        }
    }
    return g_strv_builder_end(uuids);
}
