/* SPDX-License-Identifier: MIT */
#include "pnv-daemon.h"

typedef struct {
    NMVpnServicePlugin parent;
} PnvPlugin;

typedef struct {
    NMVpnServicePluginClass parent;
} PnvPluginClass;

G_DEFINE_TYPE(PnvPlugin, pnv_plugin, NM_TYPE_VPN_SERVICE_PLUGIN)

typedef struct {
    char *name;
    PnvCleanup step;
} CleanupStep;

struct PnvSession {
    NMVpnServicePlugin *plugin;
    NMConnection *connection;
    const PnvProtocol *protocol;
    GPtrArray *cleanups;
};

static const PnvProtocol *const *protocols;
static PnvSession session;

#ifdef PNV_TEST_HOOKS
/* Found by the packages job: a package must never contain the test hooks. */
__attribute__((used)) static const char test_hooks_marker[] = "PNV_TEST_HOOKS_ENABLED";
#endif

NMConnection *pnv_session_connection(PnvSession *s)
{
    return s->connection;
}

const char *pnv_session_uuid(PnvSession *s)
{
    return s->connection ? nm_connection_get_uuid(s->connection) : "-";
}

void pnv_log(PnvSession *s, GLogLevelFlags level, const char *fmt, ...)
{
    g_autofree char *msg = NULL;
    va_list ap;

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    g_log(G_LOG_DOMAIN, level, "[%s] %s", s ? pnv_session_uuid(s) : "-", msg);
}

static void cleanup_step_free(gpointer data)
{
    CleanupStep *c = data;
    g_free(c->name);
    g_free(c);
}

void pnv_session_add_cleanup(PnvSession *s, const char *name, PnvCleanup step)
{
    CleanupStep *c = g_new0(CleanupStep, 1);
    c->name = g_strdup(name);
    c->step = step;
    g_ptr_array_add(s->cleanups, c);
}

void pnv_give_up(PnvSession *s, NMVpnPluginFailure code, const char *fmt, ...)
{
    g_autofree char *reason = NULL;
    va_list ap;

    va_start(ap, fmt);
    reason = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    pnv_warning(s, "giving up (%s): %s",
                code == NM_VPN_PLUGIN_FAILURE_LOGIN_FAILED   ? "login failed"
                : code == NM_VPN_PLUGIN_FAILURE_BAD_IP_CONFIG ? "bad IP config"
                                                              : "connect failed",
                reason);
    nm_vpn_service_plugin_failure(s->plugin, code);
}

static const PnvProtocol *find_protocol(NMConnection *connection, GError **error)
{
    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);
    const char *service = s_vpn ? nm_setting_vpn_get_service_type(s_vpn) : NULL;

    for (const PnvProtocol *const *p = protocols; *p; p++)
        if (g_strcmp0((*p)->service, service) == 0)
            return *p;
    g_set_error(error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                "service type %s is not handled by this daemon", service ? service : "(none)");
    return NULL;
}

#ifdef PNV_TEST_HOOKS
static gboolean test_give_up(gpointer data G_GNUC_UNUSED)
{
    pnv_give_up(&session, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED, "test hook: give up as requested");
    return G_SOURCE_REMOVE;
}

static void test_slow_cleanup(PnvSession *s)
{
    g_usleep(3 * G_USEC_PER_SEC);
    pnv_message(s, "test hook: slow cleanup step done");
}

static void test_hooks(PnvSession *s)
{
    const char *hook = nm_setting_vpn_get_data_item(nm_connection_get_setting_vpn(s->connection), "pnv-test-hook");

    pnv_session_add_cleanup(s, "test hook: slow step", test_slow_cleanup);
    if (g_strcmp0(hook, "give-up") == 0)
        g_idle_add(test_give_up, NULL);
}
#endif

static gboolean real_connect(NMVpnServicePlugin *plugin G_GNUC_UNUSED, NMConnection *connection, GError **error)
{
    const PnvProtocol *protocol = find_protocol(connection, error);

    if (!protocol) {
        pnv_warning(NULL, "Connect refused for %s: %s", nm_connection_get_uuid(connection), (*error)->message);
        return FALSE;
    }
    session.connection = g_object_ref(connection);
    session.protocol = protocol;
    pnv_message(&session, "Connect: %s (%s)", protocol->name, nm_connection_get_id(connection));
#ifdef PNV_TEST_HOOKS
    test_hooks(&session);
#endif
    return protocol->connect(&session, error);
}

static gboolean real_need_secrets(NMVpnServicePlugin *plugin G_GNUC_UNUSED, NMConnection *connection,
                                  const char **setting_name, GError **error)
{
    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);
    const PnvProtocol *protocol = find_protocol(connection, error);

    if (!protocol)
        return FALSE;
    for (const PnvSecret *secret = pnv_service_secrets(protocol->service); secret && secret->key; secret++) {
        if (!pnv_secret_missing(s_vpn, secret->key))
            continue;
        pnv_message(NULL, "%s: secret %s is missing, asking NM for it", nm_connection_get_uuid(connection),
                    secret->key);
        *setting_name = NM_SETTING_VPN_SETTING_NAME;
        return TRUE;
    }
    return FALSE;
}

/* Synchronous: libnm quits the main loop right after STOPPED, so nothing may be left for later (0.7). */
static gboolean real_disconnect(NMVpnServicePlugin *plugin G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    pnv_message(&session, "Disconnect");
    for (guint i = session.cleanups->len; i > 0; i--) {
        CleanupStep *c = session.cleanups->pdata[i - 1];
        pnv_debug(&session, "cleanup: %s", c->name);
        c->step(&session);
    }
    g_ptr_array_set_size(session.cleanups, 0);
    pnv_message(&session, "Disconnect done");
    return TRUE;
}

static void pnv_plugin_init(PnvPlugin *self G_GNUC_UNUSED)
{
}

static void pnv_plugin_class_init(PnvPluginClass *klass)
{
    NMVpnServicePluginClass *parent = NM_VPN_SERVICE_PLUGIN_CLASS(klass);

    parent->connect = real_connect;
    parent->need_secrets = real_need_secrets;
    parent->disconnect = real_disconnect;
}

static void on_quit(NMVpnServicePlugin *plugin G_GNUC_UNUSED, gpointer loop)
{
    g_main_loop_quit(loop);
}

int pnv_daemon_main(int argc, char **argv, const PnvProtocol *const *table)
{
    g_autofree char *bus_name = NULL;
    gboolean debug = FALSE;
    GOptionEntry options[] = {
        {"bus-name", 0, 0, G_OPTION_ARG_STRING, &bus_name, "D-Bus name to use for this instance", "NAME"},
        {"debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Log debug messages", NULL},
        {NULL},
    };
    g_autoptr(GOptionContext) context = g_option_context_new(NULL);
    g_autoptr(GError) error = NULL;
    g_autoptr(GMainLoop) loop = NULL;

    g_option_context_add_main_entries(context, options, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        return 1;
    }
    if (debug)
        g_setenv("G_MESSAGES_DEBUG", G_LOG_DOMAIN, TRUE);

    protocols = table;
    session.cleanups = g_ptr_array_new_with_free_func(cleanup_step_free);
    session.plugin = g_initable_new(pnv_plugin_get_type(), NULL, &error,
                                    NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME, bus_name ? bus_name : table[0]->service,
                                    NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, TRUE, NULL);
    if (!session.plugin) {
        g_printerr("cannot start: %s\n", error->message);
        return 1;
    }
    pnv_message(NULL, "%s on %s", pnv_version(), bus_name ? bus_name : table[0]->service);

    loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(session.plugin, "quit", G_CALLBACK(on_quit), loop);
    g_main_loop_run(loop);
    pnv_message(&session, "exit");
    return 0;
}
