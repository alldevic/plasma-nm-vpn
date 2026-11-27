/* SPDX-License-Identifier: MIT */
/* The server IP (2.4): IPv4 preferred, IPv6 only without an A record, the time limit, the cache. */
#include <glib/gstdio.h>

#include "pnv-daemon.h"

/* A resolver that answers from a table after a delay. */
typedef struct {
    GResolver parent;
    const char *const *v4, *const *v6;
    guint delay_ms;
} StubResolver;

typedef struct {
    GResolverClass parent;
} StubResolverClass;

G_DEFINE_TYPE(StubResolver, stub_resolver, G_TYPE_RESOLVER)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(StubResolver, g_object_unref)

typedef struct {
    GResolverNameLookupFlags flags;
    gint64 due;
} Pending;

/* Answers once the delay is over; like GLib's threaded resolver, a cancelled lookup completes at once. */
static gboolean answer(gpointer data)
{
    GTask *task = data;
    StubResolver *r = g_task_get_source_object(task);
    Pending *p = g_task_get_task_data(task);
    const char *const *table = p->flags & G_RESOLVER_NAME_LOOKUP_FLAGS_IPV4_ONLY ? r->v4 : r->v6;
    GList *list = NULL;

    if (g_get_monotonic_time() < p->due && !g_cancellable_is_cancelled(g_task_get_cancellable(task)))
        return G_SOURCE_CONTINUE;
    for (const char *const *a = table; a && *a; a++)
        list = g_list_append(list, g_inet_address_new_from_string(*a));
    if (list)
        g_task_return_pointer(task, list, (GDestroyNotify)g_resolver_free_addresses);
    else
        g_task_return_new_error(task, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND, "no such name");
    g_object_unref(task);
    return G_SOURCE_REMOVE;
}

static void lookup_async(GResolver *resolver, const char *name G_GNUC_UNUSED, GResolverNameLookupFlags flags,
                         GCancellable *cancel, GAsyncReadyCallback callback, gpointer data)
{
    GTask *task = g_task_new(resolver, cancel, callback, data);
    GSource *timer = g_timeout_source_new(10);
    Pending *p = g_new(Pending, 1);

    *p = (Pending){flags, g_get_monotonic_time() + ((StubResolver *)resolver)->delay_ms * G_TIME_SPAN_MILLISECOND};
    g_task_set_task_data(task, p, g_free);
    g_source_set_callback(timer, answer, task, NULL);
    g_source_attach(timer, g_main_context_get_thread_default());
    g_source_unref(timer);
}

static GList *lookup_finish(GResolver *resolver G_GNUC_UNUSED, GAsyncResult *result, GError **error)
{
    return g_task_propagate_pointer(G_TASK(result), error);
}

static void stub_resolver_init(StubResolver *self G_GNUC_UNUSED)
{
}

static void stub_resolver_class_init(StubResolverClass *klass)
{
    G_RESOLVER_CLASS(klass)->lookup_by_name_with_flags_async = lookup_async;
    G_RESOLVER_CLASS(klass)->lookup_by_name_with_flags_finish = lookup_finish;
}

static char *resolve(const char *const *v4, const char *const *v6, guint delay_ms, guint timeout_ms, GError **error)
{
    g_autoptr(StubResolver) r = g_object_new(stub_resolver_get_type(), NULL);

    r->v4 = v4;
    r->v6 = v6;
    r->delay_ms = delay_ms;
    return pnv_resolve_server(G_RESOLVER(r), "srv.stand.test", timeout_ms, error);
}

static void test_pick(void)
{
    static const char *const a[] = {"198.51.100.1", "198.51.100.2", NULL};
    static const char *const aaaa[] = {"2001:db8:100::1", NULL};
    g_autoptr(GError) error = NULL;
    g_autofree char *both = resolve(a, aaaa, 10, 1000, NULL);
    g_autofree char *only6 = resolve(NULL, aaaa, 10, 1000, NULL);
    g_autofree char *none = resolve(NULL, NULL, 10, 1000, &error);
    gint64 start = g_get_monotonic_time();
    g_autoptr(GError) late = NULL;
    g_autofree char *slow = resolve(a, aaaa, 2000, 300, &late);

    g_assert_cmpstr(both, ==, "198.51.100.1");
    g_assert_cmpstr(only6, ==, "2001:db8:100::1");
    g_assert_null(none);
    g_assert_error(error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND);
    g_assert_null(slow);
    g_assert_error(late, G_RESOLVER_ERROR, G_RESOLVER_ERROR_TEMPORARY_FAILURE);
    g_assert_cmpint(g_get_monotonic_time() - start, <, G_USEC_PER_SEC);
}

static void test_cache(void)
{
    g_autofree char *dir = g_dir_make_tmp("pnv-cache-XXXXXX", NULL);
    const char *uuid = "5ca1ab1e-0000-4000-8000-000000000001";
    g_autofree char *ip = NULL, *other = NULL, *missing = NULL, *path = g_build_filename(dir, uuid, NULL);

    g_assert_true(pnv_cache_write(dir, uuid, "srv.stand.test", "198.51.100.1", NULL));
    ip = pnv_cache_read(dir, uuid, "srv.stand.test");
    other = pnv_cache_read(dir, uuid, "new.stand.test");
    missing = pnv_cache_read(dir, "5ca1ab1e-0000-4000-8000-000000000002", "srv.stand.test");
    g_assert_cmpstr(ip, ==, "198.51.100.1");
    g_assert_null(other);
    g_assert_null(missing);
    g_unlink(path);
    g_rmdir(dir);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/server/pick", test_pick);
    g_test_add_func("/server/cache", test_cache);
    return g_test_run();
}
