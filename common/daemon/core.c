/* SPDX-License-Identifier: MIT */
/* Connect, steps 1–3 (design.md, "Core supervision and cleanup"): orphan cleanup with the takeover marker, the
 * run directory, scope and guard unit, the TUN, then the core unit, its readiness and its supervision. Every
 * resource registers its cleanup step as it is made; disconnect runs them backwards and synchronously. */
#include <arpa/inet.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#include "session.h"

#define ORPHAN_WAIT_MS 10000
#define RESOLVE_WAIT_MS 5000
#define READY_WAIT_MS 15000
#define STOP_WAIT_MS 10000
#define DEFAULT_MTU 1400


static GVariant *strv(const char *a, const char *b)
{
    const char *v[] = {a, b, NULL};
    return g_variant_new_strv(v, -1);
}

const char *pnv_session_tun(PnvSession *s)
{
    return s->tun;
}

const char *pnv_session_server_ip(PnvSession *s)
{
    return s->server_ip;
}

const char *pnv_session_tun_cidr(PnvSession *s)
{
    return s->tun_cidr;
}

const char *pnv_session_parent(PnvSession *s)
{
    return s->parent;
}

guint pnv_session_mtu(PnvSession *s)
{
    return s->profile->mtu ? s->profile->mtu : s->protocol->default_mtu ? s->protocol->default_mtu : DEFAULT_MTU;
}

static void stop_guard(PnvSession *s)
{
    pnv_unit_stop(s->guard);
    if (!pnv_unit_wait_down(s->guard, STOP_WAIT_MS))
        pnv_warning(s, "%s is still up after %d s", s->guard, STOP_WAIT_MS / 1000);
}

static void remove_run_dir(PnvSession *s)
{
    pnv_run_dir_remove(s->run_dir);
}

static void delete_tun(PnvSession *s)
{
    pnv_tun_delete(s->tun);
}

static void remove_guard(PnvSession *s)
{
    pnv_guard_remove(s->uuid);
}

/* After success only an explicit disconnect removes the kill switch; a give-up, NM going away or an unknown
 * reason leaves it (fail closed). A session that never succeeded never leaves it behind. */
static void decide_kill_switch(PnvSession *s)
{
    gboolean succeeded = pnv_state_get(s->run_dir, "succeeded");
    gboolean explicit = !s->give_up && (s->deactivation_reason == NM_ACTIVE_CONNECTION_STATE_REASON_USER_DISCONNECTED ||
                                        s->deactivation_reason == NM_ACTIVE_CONNECTION_STATE_REASON_CONNECTION_REMOVED);

    if (!s->kill_switch)
        return;
    if (succeeded && !explicit) {
        pnv_warning(s, "the kill switch stays in place (fail closed): the session ended without an explicit "
                       "disconnect (reason %u%s)",
                    s->deactivation_reason, s->give_up ? ", gave up" : "");
        return;
    }
    pnv_killswitch_remove(s->uuid);
    pnv_message(s, "kill switch removed (%s)", succeeded ? "explicit disconnect" : "the session never succeeded");
}

static PnvIp4Plan *ip4_plan(PnvSession *s)
{
    NMSettingIPConfig *s_ip4 = nm_connection_get_setting_ip4_config(s->connection);

    return pnv_ip4_plan(s->protocol->routes ? s->protocol->routes(s) : NULL,
                        s_ip4 && nm_setting_ip_config_get_never_default(s_ip4), s->profile->dns);
}

/* A default-route tunnel: the guard (2.6) and the kill switch depend on it. NM routes the default via the VPN
 * unless the profile says never-default; an AmneziaWG tunnel also needs 0.0.0.0/0 in allowed-ips. */
static gboolean default_route(PnvSession *s)
{
    g_autoptr(PnvIp4Plan) plan = ip4_plan(s);
    return plan->default_route;
}

static void stop_core(PnvSession *s)
{
    if (s->core_watch)
        g_dbus_connection_signal_unsubscribe(pnv_bus(), s->core_watch);
    s->core_watch = 0;
    pnv_unit_stop(s->core);
    if (!pnv_unit_wait_down(s->core, STOP_WAIT_MS))
        pnv_warning(s, "%s is still up after %d s", s->core, STOP_WAIT_MS / 1000);
}

#ifdef PNV_TEST_HOOKS
/* A per-UUID chain in inet plasma_nm_vpn, owned like the kill switch chains will be (2.3, "Done when"). */
static char *test_chain(PnvSession *s)
{
    return g_strdup_printf("test-%.8s", s->uuid);
}

static gboolean nft(const char *command)
{
    g_autofree char *line = g_strdup_printf("nft %s", command);
    int status = -1;

    return g_spawn_command_line_sync(line, NULL, NULL, &status, NULL) && g_spawn_check_wait_status(status, NULL);
}

static void test_chain_remove(PnvSession *s)
{
    g_autofree char *chain = test_chain(s);
    g_autofree char *command = g_strdup_printf("delete chain inet plasma_nm_vpn %s", chain);
    nft(command);
}

static void test_chain_add(PnvSession *s)
{
    g_autofree char *chain = test_chain(s);
    g_autofree char *command = g_strdup_printf("add chain inet plasma_nm_vpn %s", chain);

    pnv_session_add_cleanup(s, "test hook: chain", test_chain_remove);
    if (nft("add table inet plasma_nm_vpn") && nft(command))
        pnv_message(s, "test hook: chain %s added", chain);
}
#endif

/* Step 1. The previous instance of this profile, if any, must be gone first; it is never touched while alive. */
static gboolean cleanup_orphans(PnvSession *s, GError **error)
{
    const char *const units[] = {s->scope, s->guard, s->core};
    g_autofree char *owner = NULL;
    gboolean takeover = FALSE;
    gint64 deadline = g_get_monotonic_time() + ORPHAN_WAIT_MS * G_TIME_SPAN_MILLISECOND;

    if (!pnv_unit_down(s->scope)) {
        g_mkdir_with_parents(s->run_dir, 0700);
        takeover = pnv_state_set(s->run_dir, "takeover", TRUE);
        pnv_message(s, "the previous instance is still active: takeover marker placed, waiting for it");
    }
    for (gsize i = 0; i < G_N_ELEMENTS(units); i++) {
        gint64 left = (deadline - g_get_monotonic_time()) / G_TIME_SPAN_MILLISECOND;
        if (!pnv_unit_wait_down(units[i], MAX(left, 0))) {
            if (takeover)
                pnv_state_set(s->run_dir, "takeover", FALSE);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                        "the previous instance of this profile is still up after %d s (%s)", ORPHAN_WAIT_MS / 1000,
                        units[i]);
            return FALSE;
        }
    }
    for (gsize i = 0; i < G_N_ELEMENTS(units); i++)
        pnv_unit_reset_failed(units[i]);
    if (g_file_test(s->run_dir, G_FILE_TEST_IS_DIR)) {
        pnv_message(s, "removing the leftovers of an earlier session");
        if (!pnv_daemon_gone(s->uuid))
            pnv_warning(s, "%s did not stop", s->core);
    }
    owner = pnv_tun_owner(s->uuid);
    if (owner) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                    "the interface name %s is in use by the active profile %s (the UUIDs share their first 8 "
                    "characters)",
                    s->tun, owner);
        return FALSE;
    }
    if (pnv_tun_exists(s->tun)) {
        pnv_message(s, "deleting the leftover interface %s", s->tun);
        pnv_tun_delete(s->tun);
    }
    {
        g_autofree char *sock = pnv_uapi_socket(s->tun);
        if (g_unlink(sock) == 0)
            pnv_message(s, "removed the leftover %s", sock);
    }
    pnv_guard_remove(s->uuid);
    return TRUE;
}

/* Step 2: run directory, scope with the daemon in it, guard unit. */
static gboolean start_guard(PnvSession *s, GError **error)
{
    g_autofree char *description = g_strdup_printf("plasma-nm-vpn daemon for %s", s->uuid);
    g_autofree char *guard_description = g_strdup_printf("plasma-nm-vpn guard for %s", s->uuid);
    const char *guard_argv[] = {PNV_GUARD, "--daemon-gone", s->uuid, NULL};
    guint32 pid = getpid();
    GVariantBuilder p;

    g_mkdir_with_parents(PNV_RUN_DIR, 0755);
    if (g_mkdir(s->run_dir, 0700) != 0 || g_chmod(s->run_dir, 0700) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "cannot create %s: %s", s->run_dir,
                    g_strerror(errno));
        return FALSE;
    }
    pnv_session_add_cleanup(s, "guard unit", stop_guard);
    pnv_session_add_cleanup(s, "run directory", remove_run_dir);

    g_variant_builder_init(&p, G_VARIANT_TYPE("a(sv)"));
    pnv_prop(&p, "Description", g_variant_new_string(description));
    pnv_prop(&p, "PIDs", g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, &pid, 1, sizeof pid));
    pnv_prop(&p, "CollectMode", g_variant_new_string("inactive-or-failed"));
    if (!pnv_unit_start(s->scope, &p, error))
        return FALSE;

    g_variant_builder_init(&p, G_VARIANT_TYPE("a(sv)"));
    pnv_prop(&p, "Description", g_variant_new_string(guard_description));
    pnv_prop(&p, "Type", g_variant_new_string("oneshot"));
    pnv_prop(&p, "RemainAfterExit", g_variant_new_boolean(TRUE));
    pnv_prop(&p, "ExecStop", pnv_command(guard_argv));
    pnv_prop(&p, "BindsTo", strv(s->scope, NULL));
    pnv_prop(&p, "After", strv(s->scope, NULL));
    pnv_prop(&p, "CollectMode", g_variant_new_string("inactive-or-failed"));
    return pnv_unit_start(s->guard, &p, error);
}

/* An IP literal is used as it is; a name is resolved within 5 s, else the cached IP of an earlier successful
 * session stands in; with neither, Connect fails before anything else is set up. */
static gboolean resolve(PnvSession *s, GError **error)
{
    g_autoptr(GResolver) resolver = NULL;
    g_autoptr(GError) local = NULL;
    g_autofree char *path = g_build_filename(s->run_dir, "server-ip", NULL);

    if (g_hostname_is_ip_address(s->profile->server)) {
        s->server_ip = g_strdup(s->profile->server);
        s->server_literal = TRUE;
    } else {
        resolver = g_resolver_get_default();
        s->server_ip = pnv_resolve_server(resolver, s->profile->server, RESOLVE_WAIT_MS, &local);
        if (!s->server_ip) {
            s->server_ip = pnv_cache_read(PNV_CACHE_DIR, s->uuid, s->profile->server);
            if (!s->server_ip) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "cannot resolve the server name (%s) and there is no cached IP for it", local->message);
                return FALSE;
            }
            pnv_message(s, "cannot resolve the server name (%s): using the cached IP %s", local->message,
                        s->server_ip);
        }
    }
    pnv_message(s, "server IP %s", s->server_ip);
    /* The reconnect cycle (5.5) reuses this session's IP. */
    return g_file_set_contents_full(path, s->server_ip, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
}

/* Our own VPN connection: the newest activation of this profile. */
static NMActiveConnection *own_connection(PnvSession *s)
{
    const GPtrArray *acs = s->client ? nm_client_get_active_connections(s->client) : NULL;
    NMActiveConnection *own = NULL;

    for (guint i = 0; acs && i < acs->len; i++) {
        NMActiveConnection *ac = acs->pdata[i];
        if (nm_active_connection_get_vpn(ac) && g_strcmp0(nm_active_connection_get_uuid(ac), s->uuid) == 0 &&
            (!own || strcmp(nm_object_get_path(NM_OBJECT(ac)), nm_object_get_path(NM_OBJECT(own))) > 0))
            own = ac;
    }
    return own;
}

/* One call marks success; until the liveness check (5.4) it runs on ACTIVATED. It also refreshes the cached
 * server IP, which nothing writes earlier. */
void pnv_session_succeeded(PnvSession *s)
{
    g_autoptr(GError) error = NULL;

    pnv_state_set(s->run_dir, "succeeded", TRUE);
    pnv_message(s, "session succeeded");
    if (!s->server_literal && !pnv_cache_write(PNV_CACHE_DIR, s->uuid, s->profile->server, s->server_ip, &error))
        pnv_warning(s, "cannot cache the server IP: %s", error->message);
}

/* NM tells why the VPN goes down (0.7: before Disconnect reaches the daemon); only a user's disconnect and the
 * removal of the profile count as explicit. */
static void activation_changed(NMActiveConnection *ac G_GNUC_UNUSED, guint state, guint reason, gpointer data)
{
    PnvSession *s = data;

    if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED && !pnv_state_get(s->run_dir, "succeeded"))
        pnv_session_succeeded(s);
    if (state >= NM_ACTIVE_CONNECTION_STATE_DEACTIVATING && !s->deactivation_reason)
        s->deactivation_reason = reason;
}

static void find_parent(PnvSession *s)
{
    const GPtrArray *devices;

    s->ac = own_connection(s);
    devices = s->ac ? nm_active_connection_get_devices(s->ac) : NULL;
    if (devices && devices->len)
        s->parent = g_strdup(nm_device_get_ip_iface(devices->pdata[0]));
    if (s->ac) {
        g_object_ref(s->ac);
        g_signal_connect(s->ac, "state-changed", G_CALLBACK(activation_changed), s);
    }
    if (s->parent)
        pnv_message(s, "parent device %s", s->parent);
    else
        pnv_warning(s, "no parent device for this connection");
}

static gboolean reserve_subnet(PnvSession *s, GError **error)
{
    guint32 a;

    if (s->protocol->tun_address) {
        if (!s->protocol->tun_address(s, &s->tun_address, &s->tun_prefix)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the profile has no IPv4 TUN address");
            return FALSE;
        }
    } else {
        if (!pnv_pool_reserve(s->uuid, s->run_dir, &s->network, error))
            return FALSE;
        s->tun_address = s->network + 1;
        s->tun_prefix = 30;
    }
    a = s->tun_address;
    g_snprintf(s->tun_cidr, sizeof s->tun_cidr, "%u.%u.%u.%u/%u", a >> 24, a >> 16 & 0xff, a >> 8 & 0xff, a & 0xff,
               s->tun_prefix);
    pnv_message(s, "TUN address %s", s->tun_cidr);
    return TRUE;
}

gboolean pnv_session_prepare(PnvSession *s, GError **error)
{
    s->run_dir = pnv_run_dir(s->uuid);
    s->scope = pnv_unit_name("daemon", s->uuid);
    s->guard = pnv_unit_name("guard", s->uuid);
    s->core = pnv_unit_name("core", s->uuid);
    pnv_tun_name(s->uuid, s->tun);
    if (!cleanup_orphans(s, error) || !start_guard(s, error))
        return FALSE;
#ifdef PNV_TEST_HOOKS
    test_chain_add(s);
#endif
    if (!resolve(s, error) || !reserve_subnet(s, error))
        return FALSE;
    find_parent(s);
    /* Removed after the TUN on disconnect, so they are registered first; installed after it, before the core. */
    pnv_session_add_cleanup(s, "kill switch", decide_kill_switch);
    pnv_session_add_cleanup(s, "DNS and IPv6 guard", remove_guard);
    pnv_session_add_cleanup(s, "TUN", delete_tun);
    if (!pnv_tun_create(s->tun, pnv_session_mtu(s), error))
        return FALSE;
    if (!default_route(s)) {
        pnv_message(s, "never-default: no DNS and IPv6 guard%s",
                    s->profile->kill_switch ? ", and kill-switch=yes is ignored (only for default-route tunnels)" : "");
        return TRUE;
    }
    if (!pnv_guard_install(s->uuid, error))
        return FALSE;
    pnv_message(s, "DNS and IPv6 guard installed");
    if (!s->profile->kill_switch)
        return TRUE;
    /* Until 5.2: leftover chains of this profile are replaced in the same transaction. */
    if (!pnv_killswitch_install(s->uuid, s->profile->allow_lan, error))
        return FALSE;
    s->kill_switch = TRUE;
    pnv_message(s, "kill switch installed (allow-lan=%s)", s->profile->allow_lan ? "yes" : "no");
    return TRUE;
}

static void core_changed(GDBusConnection *bus G_GNUC_UNUSED, const char *sender G_GNUC_UNUSED,
                         const char *path G_GNUC_UNUSED, const char *interface G_GNUC_UNUSED,
                         const char *signal G_GNUC_UNUSED, GVariant *params, gpointer data)
{
    PnvSession *s = data;
    g_autoptr(GVariant) changed = g_variant_get_child_value(params, 1);
    const char *state = NULL;

    if (!s->core_ready || !g_variant_lookup(changed, "ActiveState", "&s", &state))
        return;
    if (g_str_equal(state, "failed") || g_str_equal(state, "inactive") || g_str_equal(state, "deactivating"))
        /* Until 5.5 brings the reconnect cycle. */
        pnv_give_up(s, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED, "%s stopped (%s); see journalctl -u %s", s->core,
                    state, s->core);
}

static void watch_core(PnvSession *s)
{
    g_autofree char *path = pnv_unit_path(s->core);
    g_autoptr(GVariant) r = NULL;

    if (!path)
        return;
    r = g_dbus_connection_call_sync(pnv_bus(), "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                                    "org.freedesktop.systemd1.Manager", "Subscribe", NULL, NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    s->core_watch = g_dbus_connection_signal_subscribe(pnv_bus(), "org.freedesktop.systemd1",
                                                       "org.freedesktop.DBus.Properties", "PropertiesChanged", path,
                                                       "org.freedesktop.systemd1.Unit", G_DBUS_SIGNAL_FLAGS_NONE,
                                                       core_changed, s, NULL);
}

gboolean pnv_session_start_core(PnvSession *s, const PnvCore *core, const char *config, GError **error)
{
    g_autofree char *config_path = g_build_filename(s->run_dir, "config.json", NULL);
    g_autofree char *binary = g_path_is_absolute(core->binary) ? g_strdup(core->binary)
                                                                : g_build_filename(PNV_CORE_DIR, core->binary, NULL);
    g_autofree char *description = g_strdup_printf("plasma-nm-vpn core for %s", s->uuid);
    g_autoptr(GPtrArray) argv = g_ptr_array_new();
    GVariantBuilder p, credentials;

#ifdef PNV_TEST_HOOKS
    if (g_strcmp0(s->test_hook, "broken-config") == 0)
        config = "{\"broken\":";
#endif
    if (config && !g_file_set_contents_full(config_path, config, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error))
        return FALSE;
    g_ptr_array_add(argv, binary);
    for (const char *const *a = core->args; a && *a; a++)
        g_ptr_array_add(argv, (gpointer)*a);
    g_ptr_array_add(argv, NULL);
    g_variant_builder_init(&credentials, G_VARIANT_TYPE("a(ss)"));
    g_variant_builder_add(&credentials, "(ss)", "config.json", config_path);

    g_variant_builder_init(&p, G_VARIANT_TYPE("a(sv)"));
    pnv_prop(&p, "Description", g_variant_new_string(description));
    pnv_prop(&p, "ExecStart", pnv_command((const char *const *)argv->pdata));
    if (config)
        pnv_prop(&p, "LoadCredential", g_variant_builder_end(&credentials));
    else
        g_variant_builder_clear(&credentials);
    pnv_prop(&p, "User", g_variant_new_string(PNV_CORE_USER));
    pnv_prop(&p, "AmbientCapabilities", g_variant_new_uint64(core->capabilities));
    pnv_prop(&p, "CapabilityBoundingSet", g_variant_new_uint64(core->capabilities));
    pnv_prop(&p, "NoNewPrivileges", g_variant_new_boolean(TRUE));
    pnv_prop(&p, "ProtectSystem", g_variant_new_string("strict"));
    pnv_prop(&p, "ProtectHome", g_variant_new_string("yes"));
    pnv_prop(&p, "PrivateTmp", g_variant_new_boolean(TRUE));
    if (core->read_write_paths)
        pnv_prop(&p, "ReadWritePaths", g_variant_new_strv(core->read_write_paths, -1));
    /* SIGTERM would let sing-box drop the TUN address and with it NM's routes (0.4). */
    pnv_prop(&p, "KillSignal", g_variant_new_int32(SIGKILL));
    pnv_prop(&p, "BindsTo", strv(s->scope, NULL));
    pnv_prop(&p, "After", strv(s->scope, s->guard));
    pnv_prop(&p, "CollectMode", g_variant_new_string("inactive-or-failed"));
    pnv_session_add_cleanup(s, "core unit", stop_core);
    if (!pnv_unit_start(s->core, &p, error))
        return FALSE;
    watch_core(s);

    for (guint t = 0; t < READY_WAIT_MS; t += 100) {
        if (pnv_tun_carrier(s->tun) == 1) {
            s->core_ready = TRUE;
            pnv_message(s, "%s attached to %s", s->core, s->tun);
            return TRUE;
        }
        if (t >= 500 && pnv_unit_down(s->core)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s exited before attaching to %s; see journalctl -u %s",
                        s->core, s->tun, s->core);
            return FALSE;
        }
        g_usleep(100 * 1000);
    }
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "%s did not attach to %s within %d s; see journalctl -u %s",
                s->core, s->tun, READY_WAIT_MS / 1000, s->core);
    return FALSE;
}

static GVariant *ip4(const char *text)
{
    struct in_addr a;
    inet_pton(AF_INET, text, &a);
    return g_variant_new_uint32(a.s_addr);
}

/* Sent once the core has attached (and, from 2.6, the chains are in place). NM sets the address, routes and DNS
 * itself; the route to the gateway goes via the parent device. */
void pnv_session_send_config(PnvSession *s)
{
    g_autoptr(PnvIp4Plan) plan = ip4_plan(s);
    GVariantBuilder b, servers, routes;
    struct in6_addr a6;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_TUNDEV, g_variant_new_string(s->tun));
    if (inet_pton(AF_INET6, s->server_ip, &a6) == 1)
        g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY,
                              g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, &a6, sizeof a6, 1));
    else
        g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY, ip4(s->server_ip));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP4, g_variant_new_boolean(TRUE));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP6, g_variant_new_boolean(FALSE));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CAN_PERSIST, g_variant_new_boolean(TRUE));
    if (s->profile->mtu)
        g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_MTU, g_variant_new_uint32(s->profile->mtu));
    nm_vpn_service_plugin_set_config(s->plugin, g_variant_builder_end(&b));

    g_variant_builder_init(&servers, G_VARIANT_TYPE("au"));
    for (guint i = 0; i < plan->dns->len; i++)
        g_variant_builder_add_value(&servers, ip4(plan->dns->pdata[i]));
    g_variant_builder_init(&routes, G_VARIANT_TYPE("aau"));
    for (guint i = 0; i < plan->routes->len; i++) {
        PnvCidr4 c = g_array_index(plan->routes, PnvCidr4, i);
        guint32 route[] = {htonl(c.address), c.prefix, 0, 0};
        g_variant_builder_add_value(&routes, g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, route, 4, sizeof *route));
    }
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_ADDRESS, g_variant_new_uint32(htonl(s->tun_address)));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_PREFIX, g_variant_new_uint32(s->tun_prefix));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_DNS, g_variant_builder_end(&servers));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_ROUTES, g_variant_builder_end(&routes));
    if (plan->never_default)
        g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_NEVER_DEFAULT, g_variant_new_boolean(TRUE));
    nm_vpn_service_plugin_set_ip4_config(s->plugin, g_variant_builder_end(&b));
    pnv_message(s, "Config and Ip4Config sent");
}
