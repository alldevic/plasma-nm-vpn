/* SPDX-License-Identifier: MIT */
/* /run/plasma-nm-vpn/guard-status (design.md, "Leak protection"): the blocks left in place, for the editor's
 * banner. World-readable, so it names profiles by UUID only. A GLib key file with one group per UUID:
 *   [<uuid>] version=1, state=blocked, reason=gave-up|daemon-gone|inherited, since=<unix time>
 * Several writers (daemons, --daemon-gone, --clear) update it under a lock and replace it atomically; groups
 * of a newer version are kept as they are. */
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/file.h>
#include <unistd.h>

#include "pnv-daemon.h"

#define STATUS PNV_RUN_DIR "/guard-status"
#define STATUS_VERSION 1

typedef gboolean (*Update)(GKeyFile *status, const char *uuid, const char *reason);

static gboolean update(const char *uuid, const char *reason, Update change)
{
    g_autoptr(GKeyFile) status = g_key_file_new();
    g_autofree char *text = NULL;
    int lock;
    gboolean ok = FALSE;

    g_mkdir_with_parents(PNV_RUN_DIR, 0755);
    lock = open(PNV_RUN_DIR "/guard-status.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX) != 0) {
        g_warning("[%s] cannot lock guard-status: %s", uuid, g_strerror(errno));
        if (lock >= 0)
            close(lock);
        return FALSE;
    }
    g_key_file_load_from_file(status, STATUS, G_KEY_FILE_KEEP_COMMENTS, NULL);
    if (change(status, uuid, reason)) {
        text = g_key_file_to_data(status, NULL, NULL);
        /* g_file_set_contents writes a temporary file and renames it. */
        ok = g_file_set_contents_full(STATUS, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0644, NULL);
    }
    close(lock);
    return ok;
}

static gboolean add_entry(GKeyFile *status, const char *uuid, const char *reason)
{
    if (g_key_file_get_integer(status, uuid, "version", NULL) > STATUS_VERSION)
        return FALSE;
    g_key_file_set_integer(status, uuid, "version", STATUS_VERSION);
    g_key_file_set_string(status, uuid, "state", "blocked");
    g_key_file_set_string(status, uuid, "reason", reason);
    if (!g_key_file_has_key(status, uuid, "since", NULL))
        g_key_file_set_int64(status, uuid, "since", g_get_real_time() / G_USEC_PER_SEC);
    return TRUE;
}

static gboolean remove_entry(GKeyFile *status, const char *uuid, const char *reason G_GNUC_UNUSED)
{
    return g_key_file_remove_group(status, uuid, NULL);
}

/* Only a block that is really there is listed: after a --clear of a live session its give-up writes nothing. */
void pnv_status_block(const char *uuid, const char *reason)
{
    if (!pnv_killswitch_present(uuid))
        return;
    if (update(uuid, reason, add_entry))
        g_message("[%s] kill switch block left in place (%s), listed in %s", uuid, reason, STATUS);
}

void pnv_status_clear(const char *uuid)
{
    update(uuid, NULL, remove_entry);
}

/* The UUIDs listed in the file, for a bare --clear. */
GStrv pnv_status_list(void)
{
    g_autoptr(GKeyFile) status = g_key_file_new();

    if (!g_key_file_load_from_file(status, STATUS, G_KEY_FILE_NONE, NULL))
        return g_new0(char *, 1);
    return g_key_file_get_groups(status, NULL);
}
