/* SPDX-License-Identifier: MIT */
/* /run/plasma-nm-vpn/<uuid>/ and the work of plasma-nm-vpn-guard --daemon-gone, which the orphan cleanup on
 * Connect reuses for a leftover run directory. */
#include <glib/gstdio.h>

#include "pnv-daemon.h"

/* The canonical lowercase 8-4-4-4-12 form only: the UUID ends up in paths and unit names. */
gboolean pnv_uuid_valid(const char *uuid)
{
    static const char shape[] = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";

    if (!uuid || strlen(uuid) != sizeof shape - 1)
        return FALSE;
    for (gsize i = 0; shape[i]; i++)
        if (shape[i] == '-' ? uuid[i] != '-' : !(g_ascii_isdigit(uuid[i]) || (uuid[i] >= 'a' && uuid[i] <= 'f')))
            return FALSE;
    return TRUE;
}

char *pnv_run_dir(const char *uuid)
{
    return g_build_filename(PNV_RUN_DIR, uuid, NULL);
}

/* The directory holds only files (the config, markers). */
void pnv_run_dir_remove(const char *path)
{
    g_autoptr(GDir) dir = g_dir_open(path, 0, NULL);
    const char *name;

    while (dir && (name = g_dir_read_name(dir))) {
        g_autofree char *file = g_build_filename(path, name, NULL);
        g_unlink(file);
    }
    g_rmdir(path);
}

gboolean pnv_daemon_gone(const char *uuid)
{
    g_autofree char *run = pnv_run_dir(uuid);
    g_autofree char *core = pnv_unit_name("core", uuid);
    g_autofree char *socket_path = NULL;
    char tun[IFNAMSIZ];
    gboolean ok;

    if (!g_file_test(run, G_FILE_TEST_IS_DIR))
        return TRUE;
    pnv_unit_stop(core);
    ok = pnv_unit_wait_down(core, 10000);
    pnv_tun_name(uuid, tun);
    pnv_tun_delete(tun);
    g_unlink(socket_path = pnv_uapi_socket(tun));
    pnv_guard_remove(uuid);
    /* Fail closed: after a session that had succeeded, the kill switch outlives the daemon. */
    if (!pnv_state_get(run, "succeeded"))
        pnv_killswitch_remove(uuid);
#ifdef PNV_TEST_HOOKS
    {
        g_autofree char *script = g_strdup_printf("add table inet plasma_nm_vpn\n"
                                                  "destroy chain inet plasma_nm_vpn test-%.8s\n", uuid);
        pnv_nft_run(script, NULL);
    }
#endif
    pnv_run_dir_remove(run);
    return ok;
}

/* amneziawg-go's UAPI socket; kill -9 leaves it behind (0.4). */
char *pnv_uapi_socket(const char *tun)
{
    return g_strdup_printf("/run/amneziawg/%s.sock", tun);
}

/* Another profile whose UUID starts with the same 8 characters and whose daemon is alive owns the TUN name. */
char *pnv_tun_owner(const char *uuid)
{
    g_autoptr(GDir) dir = g_dir_open(PNV_RUN_DIR, 0, NULL);
    const char *name;

    while (dir && (name = g_dir_read_name(dir))) {
        g_autofree char *scope = NULL;

        if (!pnv_uuid_valid(name) || g_str_equal(name, uuid) || strncmp(name, uuid, 8) != 0)
            continue;
        scope = pnv_unit_name("daemon", name);
        if (!pnv_unit_down(scope))
            return g_strdup(name);
    }
    return NULL;
}
