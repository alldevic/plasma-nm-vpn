/* SPDX-License-Identifier: MIT */
/* The server IP (design.md, "Route to the server"): resolved before the tunnel, IPv4 preferred, with a time
 * limit; cached per profile once a session succeeds, for when resolution fails. */
#include <glib/gstdio.h>

#include "pnv-daemon.h"

typedef struct {
    GList *v4, *v6;
    GError *v4_error, *v6_error;
    gboolean v4_done, v6_done, timed_out;
} Lookup;

static void v4_done(GObject *resolver, GAsyncResult *result, gpointer data)
{
    Lookup *l = data;
    l->v4 = g_resolver_lookup_by_name_with_flags_finish(G_RESOLVER(resolver), result, &l->v4_error);
    l->v4_done = TRUE;
}

static void v6_done(GObject *resolver, GAsyncResult *result, gpointer data)
{
    Lookup *l = data;
    l->v6 = g_resolver_lookup_by_name_with_flags_finish(G_RESOLVER(resolver), result, &l->v6_error);
    l->v6_done = TRUE;
}

static gboolean timed_out(gpointer data)
{
    ((Lookup *)data)->timed_out = TRUE;
    return G_SOURCE_REMOVE;
}

/* The first IPv4 address; an IPv6 one only if there is no IPv4 address at all. */
char *pnv_pick_server_ip(GList *v4, GList *v6)
{
    GList *pick = v4 ? v4 : v6;
    return pick ? g_inet_address_to_string(pick->data) : NULL;
}

static gboolean not_found(const GError *e)
{
    return g_error_matches(e, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND);
}

/* One round of A and AAAA in parallel; an A answer decides at once, AAAA is waited for only without one. */
static char *lookup(GResolver *resolver, const char *name, GMainContext *context, guint ms, gboolean *final)
{
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_autoptr(GSource) timer = g_timeout_source_new(ms);
    Lookup l = {0};
    char *ip;

    g_source_set_callback(timer, timed_out, &l, NULL);
    g_source_attach(timer, context);
    g_resolver_lookup_by_name_with_flags_async(resolver, name, G_RESOLVER_NAME_LOOKUP_FLAGS_IPV4_ONLY, cancel, v4_done,
                                               &l);
    g_resolver_lookup_by_name_with_flags_async(resolver, name, G_RESOLVER_NAME_LOOKUP_FLAGS_IPV6_ONLY, cancel, v6_done,
                                               &l);
    while (!l.timed_out && !(l.v4_done && (l.v4 || l.v6_done)))
        g_main_context_iteration(context, TRUE);
    ip = l.v4_done ? pnv_pick_server_ip(l.v4, l.v6_done ? l.v6 : NULL) : NULL;
    /* Only "no such name" for both is an answer; anything else (refused while NM rewrites resolv.conf, a
     * server failure) is worth another round while time is left. */
    *final = ip || (l.v4_done && l.v6_done && not_found(l.v4_error) && not_found(l.v6_error));
    g_cancellable_cancel(cancel);
    while (!l.v4_done || !l.v6_done)
        g_main_context_iteration(context, TRUE);
    g_source_destroy(timer);
    g_resolver_free_addresses(l.v4);
    g_resolver_free_addresses(l.v6);
    g_clear_error(&l.v4_error);
    g_clear_error(&l.v6_error);
    return ip;
}

char *pnv_resolve_server(GResolver *resolver, const char *name, guint timeout_ms, GError **error)
{
    g_autoptr(GMainContext) context = g_main_context_new();
    gint64 deadline = g_get_monotonic_time() + timeout_ms * G_TIME_SPAN_MILLISECOND;
    gboolean final = FALSE;
    char *ip = NULL;

    g_main_context_push_thread_default(context);
    for (;;) {
        gint64 left = (deadline - g_get_monotonic_time()) / G_TIME_SPAN_MILLISECOND;
        if (left <= 0)
            break;
        ip = lookup(resolver, name, context, left, &final);
        if (final)
            break;
        g_usleep(MIN(250, MAX(left - 1, 0)) * 1000);
    }
    g_main_context_pop_thread_default(context);
    if (!ip)
        g_set_error(error, G_RESOLVER_ERROR, final ? G_RESOLVER_ERROR_NOT_FOUND : G_RESOLVER_ERROR_TEMPORARY_FAILURE,
                    final ? "%s has no address" : "%s did not resolve within %u ms", name, timeout_ms);
    return ip;
}

/* /var/lib/plasma-nm-vpn/<uuid>: the profile's server value and its IP. */
char *pnv_cache_read(const char *dir, const char *uuid, const char *server)
{
    g_autofree char *path = g_build_filename(dir, uuid, NULL);
    g_autoptr(GKeyFile) file = g_key_file_new();
    g_autofree char *name = NULL;
    char *ip;

    if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL))
        return NULL;
    name = g_key_file_get_string(file, "server", "name", NULL);
    ip = g_key_file_get_string(file, "server", "ip", NULL);
    if (g_strcmp0(name, server) != 0 || !ip || !g_hostname_is_ip_address(ip)) {
        g_free(ip);
        return NULL;
    }
    return ip;
}

gboolean pnv_cache_write(const char *dir, const char *uuid, const char *server, const char *ip, GError **error)
{
    g_autofree char *path = g_build_filename(dir, uuid, NULL);
    g_autoptr(GKeyFile) file = g_key_file_new();
    g_autofree char *text = NULL;

    g_key_file_set_string(file, "server", "name", server);
    g_key_file_set_string(file, "server", "ip", ip);
    text = g_key_file_to_data(file, NULL, NULL);
    g_mkdir_with_parents(dir, 0700);
    return g_file_set_contents_full(path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
}
