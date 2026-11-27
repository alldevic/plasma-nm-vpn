/* SPDX-License-Identifier: MIT */
/* systemd over D-Bus: transient units and their states. All calls block; disconnect relies on that. */
#include "pnv-daemon.h"

static GDBusConnection *bus;

GDBusConnection *pnv_bus(void)
{
    if (!bus)
        bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    return bus;
}

static GVariant *manager_call(const char *method, GVariant *args, const GVariantType *reply, GError **error)
{
    if (!pnv_bus()) {
        g_variant_unref(g_variant_ref_sink(args));
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "no system bus");
        return NULL;
    }
    return g_dbus_connection_call_sync(bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager", method, args, reply,
                                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);
}

char *pnv_unit_name(const char *kind, const char *uuid)
{
    return g_strdup_printf("plasma-nm-vpn-%s-%s.%s", kind, uuid, g_str_equal(kind, "daemon") ? "scope" : "service");
}

gboolean pnv_unit_start(const char *name, GVariantBuilder *props, GError **error)
{
    g_autoptr(GVariant) r = manager_call("StartTransientUnit",
                                         g_variant_new("(ssa(sv)a(sa(sv)))", name, "fail", props, NULL), NULL, error);
    return r != NULL;
}

void pnv_unit_stop(const char *name)
{
    g_autoptr(GVariant) r = manager_call("StopUnit", g_variant_new("(ss)", name, "replace"), NULL, NULL);
}

void pnv_unit_reset_failed(const char *name)
{
    g_autoptr(GVariant) r = manager_call("ResetFailedUnit", g_variant_new("(s)", name), NULL, NULL);
}

char *pnv_unit_path(const char *name)
{
    g_autoptr(GVariant) r = manager_call("GetUnit", g_variant_new("(s)", name), G_VARIANT_TYPE("(o)"), NULL);
    char *path = NULL;

    if (r)
        g_variant_get(r, "(o)", &path);
    return path;
}

/* "gone" for a unit systemd does not know (never started, or collected). */
char *pnv_unit_state(const char *name)
{
    g_autofree char *path = pnv_unit_path(name);
    g_autoptr(GVariant) r = NULL, v = NULL;

    if (path)
        r = g_dbus_connection_call_sync(bus, "org.freedesktop.systemd1", path, "org.freedesktop.DBus.Properties",
                                        "Get", g_variant_new("(ss)", "org.freedesktop.systemd1.Unit", "ActiveState"),
                                        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    if (!r)
        return g_strdup("gone");
    g_variant_get(r, "(v)", &v);
    return g_variant_dup_string(v, NULL);
}

gboolean pnv_unit_down(const char *name)
{
    g_autofree char *state = pnv_unit_state(name);
    return g_str_equal(state, "inactive") || g_str_equal(state, "failed") || g_str_equal(state, "gone");
}

gboolean pnv_unit_wait_down(const char *name, guint timeout_ms)
{
    for (guint t = 0;; t += 100) {
        if (pnv_unit_down(name))
            return TRUE;
        if (t >= timeout_ms)
            return FALSE;
        g_usleep(100 * 1000);
    }
}

void pnv_prop(GVariantBuilder *b, const char *name, GVariant *value)
{
    g_variant_builder_add(b, "(sv)", name, value);
}

/* ExecStart= and friends: a(sasb), one command, failure not ignored. */
GVariant *pnv_command(const char *const *argv)
{
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("a(sasb)"));
    g_variant_builder_add(&b, "(s^asb)", argv[0], argv, FALSE);
    return g_variant_builder_end(&b);
}
