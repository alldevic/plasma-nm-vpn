/* SPDX-License-Identifier: MIT */
/* plasma-nm-vpn-guard --daemon-gone <uuid>: the guard unit's ExecStop, run as root by systemd once the daemon's
 * scope is gone. It acts only if the run directory is still there, i.e. the daemon did not clean up itself. */
#include "pnv-daemon.h"

int main(int argc, char **argv)
{
    g_autofree char *gone = NULL, *run = NULL;
    GOptionEntry options[] = {
        {"daemon-gone", 0, 0, G_OPTION_ARG_STRING, &gone, "Clean up after the daemon of this profile", "UUID"},
        {NULL},
    };
    g_autoptr(GOptionContext) context = g_option_context_new(NULL);
    g_autoptr(GError) error = NULL;

    g_option_context_add_main_entries(context, options, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        return 2;
    }
    if (!pnv_uuid_valid(gone)) {
        g_printerr("usage: plasma-nm-vpn-guard --daemon-gone <uuid>\n");
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
