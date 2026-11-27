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
