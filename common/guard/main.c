/* SPDX-License-Identifier: MIT */
/* plasma-nm-vpn-guard:
 *   --daemon-gone <uuid>  the guard unit's ExecStop, run as root by systemd once the daemon's scope is gone; it
 *                         acts only if the run directory is still there (the daemon did not clean up itself);
 *   --clear [<uuid>]      removes a kill switch block and its guard-status entry; without a UUID every block
 *                         except those of connections NM reports as activated or activating. Run by a regular
 *                         user it re-executes itself through pkexec (polkit action, auth_admin). */
#include <errno.h>
#include <unistd.h>

#include "pnv-daemon.h"

#define PKEXEC "/usr/bin/pkexec"

static char *clear_uuid;
static gboolean clear;

static gboolean parse_clear(const char *name G_GNUC_UNUSED, const char *value, gpointer data G_GNUC_UNUSED,
                            GError **error G_GNUC_UNUSED)
{
    clear = TRUE;
    clear_uuid = g_strdup(value);
    return TRUE;
}

static void clear_one(const char *uuid)
{
    pnv_killswitch_remove(uuid);
    pnv_status_clear(uuid);
    g_message("[%s] kill switch block cleared", uuid);
}

/* The UUIDs of VPN connections NM reports as activated or activating: their protection stays. */
static GHashTable *live_connections(void)
{
    GHashTable *live = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_autoptr(GError) error = NULL;
    g_autoptr(NMClient) client = nm_client_new(NULL, &error);
    const GPtrArray *acs = client ? nm_client_get_active_connections(client) : NULL;

    if (!client)
        g_warning("no NM client (%s): every block counts as left over", error->message);
    for (guint i = 0; acs && i < acs->len; i++) {
        NMActiveConnectionState state = nm_active_connection_get_state(acs->pdata[i]);
        if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED || state == NM_ACTIVE_CONNECTION_STATE_ACTIVATING)
            g_hash_table_add(live, g_strdup(nm_active_connection_get_uuid(acs->pdata[i])));
    }
    return live;
}

static int clear_all(void)
{
    g_autoptr(GHashTable) live = live_connections();
    g_auto(GStrv) chains = pnv_killswitch_list();
    g_auto(GStrv) entries = pnv_status_list();
    g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);

    for (GStrv lists[] = {chains, entries}, *l = lists; l < lists + 2; l++)
        for (GStrv u = *l; *u; u++) {
            if (!pnv_uuid_valid(*u) || !g_hash_table_add(seen, *u))
                continue;
            if (g_hash_table_contains(live, *u))
                g_message("[%s] connection is up: its kill switch stays", *u);
            else
                clear_one(*u);
        }
    return 0;
}

/* pkexec runs us again as root with the same arguments; polkit asks for administrator credentials. */
static int elevate(int argc, char **argv)
{
    g_autoptr(GPtrArray) args = g_ptr_array_new();

    g_ptr_array_add(args, PKEXEC);
    g_ptr_array_add(args, PNV_GUARD);
    for (int i = 1; i < argc; i++)
        g_ptr_array_add(args, argv[i]);
    g_ptr_array_add(args, NULL);
    execv(PKEXEC, (char **)args->pdata);
    g_printerr("cannot run %s: %s\n", PKEXEC, g_strerror(errno));
    return 1;
}

int main(int argc, char **argv)
{
    g_autofree char *gone = NULL, *run = NULL;
    GOptionEntry options[] = {
        {"daemon-gone", 0, 0, G_OPTION_ARG_STRING, &gone, "Clean up after the daemon of this profile", "UUID"},
        {"clear", 0, G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, parse_clear,
         "Remove the kill switch block of this profile, or every left-over block", "[UUID]"},
        {NULL},
    };
    g_autoptr(GOptionContext) context = g_option_context_new(NULL);
    g_autoptr(GError) error = NULL;
    char **original = g_strdupv(argv);
    int original_argc = argc;

    g_option_context_add_main_entries(context, options, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        return 2;
    }
    if (clear) {
        if (clear_uuid && !pnv_uuid_valid(clear_uuid)) {
            g_printerr("not a profile UUID: %s\n", clear_uuid);
            return 2;
        }
        if (geteuid() != 0)
            return elevate(original_argc, original);
        if (clear_uuid) {
            clear_one(clear_uuid);
            return 0;
        }
        return clear_all();
    }
    if (!pnv_uuid_valid(gone)) {
        g_printerr("usage: plasma-nm-vpn-guard --daemon-gone <uuid> | --clear [<uuid>]\n");
        return 2;
    }
    run = pnv_run_dir(gone);
    if (!g_file_test(run, G_FILE_TEST_IS_DIR))
        return 0;
    g_message("[%s] the daemon is gone without cleaning up: cleaning up after it", gone);
    if (!pnv_daemon_gone(gone)) {
        g_warning("[%s] the core unit did not stop", gone);
        return 1;
    }
    g_message("[%s] cleaned up", gone);
    return 0;
}
