/* SPDX-License-Identifier: MIT */
/* The list files in /usr/share/plasma-nm-vpn/: UTF-8 text, one entry per line, entries made of
 * [A-Za-z0-9_.-]; blank lines and lines starting with '#' are skipped; spaces around an entry are ignored.
 * An entry may carry fields after it, separated by spaces, made of [A-Za-z0-9_.:-] (the AmneziaWG
 * parameters: "awg-jc Jc uint16"). */
#include <string.h>

#include "pnv-profile.h"

static gboolean valid_entry(const char *s)
{
    if (!*s)
        return FALSE;
    for (; *s; s++)
        if (!g_ascii_isalnum(*s) && !strchr("_.-", *s))
            return FALSE;
    return TRUE;
}

static gboolean valid_fields(GStrv fields)
{
    for (; *fields; fields++)
        for (const char *c = *fields; *c; c++)
            if (!g_ascii_isalnum(*c) && !strchr("_.:-", *c))
                return FALSE;
    return TRUE;
}

static gboolean list_fail(PnvProblem *p, PnvCode code, const char *name, gint64 line)
{
    if (p) {
        pnv_problem_clear(p);
        *p = (PnvProblem){code, NULL, g_strdup(name), line, 0, 0};
    }
    return FALSE;
}

PnvList *pnv_list_parse(const char *text, const char *name, PnvProblem *p)
{
    g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
    PnvList *list = g_new0(PnvList, 1);

    list->entries = g_ptr_array_new_with_free_func(g_free);
    list->set = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)g_strfreev);
    for (guint i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        g_auto(GStrv) words = NULL;
        guint n = 0;
        char *entry;

        if (!*line || *line == '#')
            continue;
        words = g_strsplit_set(line, " \t", -1);
        for (GStrv w = words; *w; w++)
            if (**w)
                words[n++] = *w;
            else
                g_free(*w);
        words[n] = NULL;
        if (!valid_entry(words[0]) || !valid_fields(words + 1)) {
            list_fail(p, PNV_E_LIST_ENTRY, name, i + 1);
            pnv_list_free(list);
            return NULL;
        }
        if (g_hash_table_contains(list->set, words[0]))
            continue;
        entry = g_strdup(words[0]);
        g_ptr_array_add(list->entries, entry);
        g_hash_table_insert(list->set, entry, g_strdupv(words + 1));
    }
    return list;
}

PnvList *pnv_list_load(const char *path, PnvProblem *p)
{
    g_autofree char *text = NULL;

    if (!g_file_get_contents(path, &text, NULL, NULL) || !g_utf8_validate(text, -1, NULL)) {
        list_fail(p, PNV_E_LIST_FILE, path, -1);
        return NULL;
    }
    return pnv_list_parse(text, path, p);
}

gboolean pnv_list_contains(const PnvList *list, const char *entry)
{
    return list && g_hash_table_contains(list->set, entry);
}

const char *const *pnv_list_fields(const PnvList *list, const char *entry)
{
    return list ? g_hash_table_lookup(list->set, entry) : NULL;
}

void pnv_list_free(PnvList *list)
{
    if (!list)
        return;
    g_hash_table_unref(list->set);
    g_ptr_array_unref(list->entries);
    g_free(list);
}
