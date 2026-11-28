/* SPDX-License-Identifier: MIT */
/* The session state in the run directory: a versioned key file, so that an upgraded daemon or guard reads what
 * the previous version wrote (an upgrade does not touch running connections). Markers: succeeded (5.1),
 * inherited (5.2); and the session server IP, server-ip (5.5). */
#include "pnv-daemon.h"

#define STATE_VERSION 1

static char *state_path(const char *run_dir)
{
    return g_build_filename(run_dir, "state", NULL);
}

gboolean pnv_state_get(const char *run_dir, const char *marker)
{
    g_autofree char *path = state_path(run_dir);
    g_autoptr(GKeyFile) state = g_key_file_new();

    /* Any version: the markers keep their names; a newer writer only adds keys. */
    return g_key_file_load_from_file(state, path, G_KEY_FILE_NONE, NULL) &&
           g_key_file_get_boolean(state, "session", marker, NULL);
}

static gboolean save(const char *path, GKeyFile *state)
{
    g_autofree char *text = NULL;

    if (g_key_file_get_integer(state, "session", "version", NULL) < STATE_VERSION)
        g_key_file_set_integer(state, "session", "version", STATE_VERSION);
    text = g_key_file_to_data(state, NULL, NULL);
    return g_file_set_contents_full(path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
}

gboolean pnv_state_set_string(const char *run_dir, const char *key, const char *value)
{
    g_autofree char *path = state_path(run_dir);
    g_autoptr(GKeyFile) state = g_key_file_new();

    g_key_file_load_from_file(state, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_string(state, "session", key, value);
    return save(path, state);
}

gboolean pnv_state_set(const char *run_dir, const char *marker, gboolean value)
{
    g_autofree char *path = state_path(run_dir);
    g_autoptr(GKeyFile) state = g_key_file_new();

    g_key_file_load_from_file(state, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_boolean(state, "session", marker, value);
    return save(path, state);
}
