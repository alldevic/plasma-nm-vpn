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

    if (s->inherited) {
        pnv_message(s, "the inherited block stays (the session did not succeed)");
        return;
    }
    if (!s->kill_switch)
        return;
    if (succeeded && !explicit) {
        pnv_warning(s, "the kill switch stays in place (fail closed): the session ended without an explicit "
                       "disconnect (reason %u%s)",
                    s->deactivation_reason, s->give_up ? ", gave up" : "");
        pnv_status_block(s->uuid, "gave-up");
        return;
    }
    pnv_killswitch_remove(s->uuid);
    pnv_status_clear(s->uuid);
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

void pnv_session_stop_core(PnvSession *s)
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
    gint64 deadline = g_get_monotonic_time() + ORPHAN_WAIT_MS * G_TIME_SPAN_MILLISECOND;

    if (!pnv_unit_down(s->scope))
        pnv_message(s, "the previous instance is still active: waiting for it");
    for (gsize i = 0; i < G_N_ELEMENTS(units); i++) {
        gint64 left = (deadline - g_get_monotonic_time()) / G_TIME_SPAN_MILLISECOND;
        if (!pnv_unit_wait_down(units[i], MAX(left, 0))) {
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
    pnv_temp_block_remove(s->uuid);
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

    if (g_hostname_is_ip_address(s->profile->server)) {
        s->server_ip = g_strdup(s->profile->server);
        s->server_literal = TRUE;
    } else if (s->inherited) {
        /* Under the block DNS goes nowhere but a tunnel; a lookup would bypass it or fail. */
        s->server_ip = pnv_cache_read(PNV_CACHE_DIR, s->uuid, s->profile->server);
        if (!s->server_ip) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "the kill switch of an earlier session is in place and there is no cached IP for %s; the "
                        "block stays until a successful connect or plasma-nm-vpn-guard --clear",
                        s->profile->server);
            return FALSE;
        }
        pnv_message(s, "under the inherited block: using the cached IP %s", s->server_ip);
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
    /* The reconnect cycle (5.5) reuses this session's IP; it never resolves the name again. */
    if (!pnv_state_set_string(s->run_dir, "server-ip", s->server_ip)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "cannot write the session state in %s", s->run_dir);
        return FALSE;
    }
    return TRUE;
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

/* One call marks success, when the first liveness check passes. It also refreshes the cached server IP, which
 * nothing writes earlier. */
void pnv_session_succeeded(PnvSession *s)
{
    g_autoptr(GError) error = NULL;

    pnv_state_set(s->run_dir, "succeeded", TRUE);
    pnv_message(s, "session succeeded");
    if (s->inherited) {
        g_autoptr(GError) swap = NULL;

        /* The inherited chains become this session's in one transaction, or go if the profile no longer
         * qualifies; there is no moment without either. */
        if (s->profile->kill_switch && default_route(s)) {
            if (pnv_killswitch_install(s->uuid, s->profile->allow_lan, &swap))
                s->kill_switch = TRUE;
            else
                pnv_warning(s, "cannot take over the inherited kill switch: %s", swap->message);
        } else {
            pnv_killswitch_remove(s->uuid);
        }
        pnv_message(s, "inherited block %s", s->kill_switch ? "taken over" : "removed (the profile no longer asks for it)");
        pnv_status_clear(s->uuid);
        s->inherited = FALSE;
        pnv_state_set(s->run_dir, "inherited", FALSE);
    }
    if (!s->server_literal && !pnv_cache_write(PNV_CACHE_DIR, s->uuid, s->profile->server, s->server_ip, &error))
        pnv_warning(s, "cannot cache the server IP: %s", error->message);
}

static guint liveness_timeout(void)
{
#ifdef PNV_TEST_HOOKS
    /* The harness may raise it (6.2 holds an attempt open this way). */
    g_autofree char *text = NULL;
    if (g_file_get_contents("/run/pnv-test-liveness-timeout", &text, NULL, NULL) && g_ascii_strtoull(text, NULL, 10))
        return g_ascii_strtoull(text, NULL, 10);
#endif
    return PNV_LIVENESS_TIMEOUT;
}

/* The profile's first DNS server, else 1.1.1.1: for a default-route tunnel the first default, with never-default
 * the one extra destination, reached through the TUN without a route. */
static const char *liveness_target(PnvSession *s)
{
    return s->profile->dns && s->profile->dns->len ? s->profile->dns->pdata[0] : "1.1.1.1";
}

static void liveness_run(PnvSession *s);

static void liveness_done(PnvSession *s, gboolean ok, const char *reason)
{
    if (s->live_stopped)
        return;
    if (!s->live_succeeded) {
        if (!ok) {
            pnv_give_up(s, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED, "the liveness check failed: %s", reason);
            return;
        }
        s->live_succeeded = TRUE;
        pnv_session_succeeded(s);
        return;
    }
    switch (pnv_liveness_record(&s->live_failures, ok)) {
    case PNV_LIVENESS_OK:
        return;
    case PNV_LIVENESS_FAILED:
        pnv_warning(s, "liveness check %u of %d failed: %s", s->live_failures, PNV_LIVENESS_FAILURES, reason);
        return;
    case PNV_LIVENESS_DEAD: {
        g_autofree char *trigger = g_strdup_printf("%d liveness checks in a row failed, the last: %s",
                                                   PNV_LIVENESS_FAILURES, reason);
        pnv_session_reconnect(s, trigger);
        return;
    }
    }
}

static void probe(PnvSession *s, gboolean first, PnvProbeDone done)
{
    if (s->protocol->probe)
        s->protocol->probe(s, first, liveness_timeout(), done);
    else
        pnv_dns_probe(s, liveness_target(s), liveness_timeout(), done);
}

static void liveness_run(PnvSession *s)
{
    if (!s->cycle_active)
        probe(s, !s->live_succeeded, liveness_done);
}

static gboolean liveness_tick(gpointer data)
{
    liveness_run(data);
    return G_SOURCE_CONTINUE;
}

static void stop_liveness(PnvSession *s)
{
    s->live_stopped = TRUE;
    s->cycle_active = FALSE;
    g_clear_handle_id(&s->live_timer, g_source_remove);
    g_clear_handle_id(&s->cycle_timer, g_source_remove);
    g_clear_handle_id(&s->pending_timer, g_source_remove);
    if (s->sleep_watch)
        g_dbus_connection_signal_unsubscribe(pnv_bus(), s->sleep_watch);
    s->sleep_watch = 0;
}

/* The reconnect cycle (design.md, "Network changes and sleep"): STARTING, a parent device, the core recreated
 * with this session's server IP and the current binding, Config and Ip4Config, then the liveness check; retried
 * with a growing backoff until CYCLE_TIMEOUT_S, then a give-up (with the success marker: fail closed). */
#define CYCLE_TIMEOUT_S 120
#define BACKOFF_MAX_S 16

static GVariant *nm_property(const char *path, const char *interface, const char *name)
{
    g_autoptr(GVariant) r = g_dbus_connection_call_sync(pnv_bus(), NM_DBUS_SERVICE, path,
                                                        "org.freedesktop.DBus.Properties", "Get",
                                                        g_variant_new("(ss)", interface, name), G_VARIANT_TYPE("(v)"),
                                                        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    GVariant *value = NULL;

    if (r)
        g_variant_get(r, "(v)", &value);
    return value;
}

/* The VPN's parent device, asked from NM itself: libnm's cache was seen lagging behind an assignment that NM had
 * already made (an empty device list for good), which would leave the cycle waiting for nothing. */
static char *current_parent(PnvSession *s)
{
    g_autoptr(GVariant) devices = s->ac ? nm_property(nm_object_get_path(NM_OBJECT(s->ac)),
                                                      NM_DBUS_INTERFACE_ACTIVE_CONNECTION, "Devices")
                                        : NULL;
    g_autoptr(GVariant) iface = NULL;
    const char *device;

    if (!devices || !g_variant_is_of_type(devices, G_VARIANT_TYPE("ao")) || g_variant_n_children(devices) == 0)
        return NULL;
    g_variant_get_child(devices, 0, "&o", &device);
    iface = nm_property(device, NM_DBUS_INTERFACE_DEVICE, "IpInterface");
    return iface && g_variant_is_of_type(iface, G_VARIANT_TYPE_STRING) && *g_variant_get_string(iface, NULL)
               ? g_variant_dup_string(iface, NULL)
               : NULL;
}

static void cycle_step1(PnvSession *s);
static void cycle_step2(PnvSession *s);
static void remove_temp_block(PnvSession *s);

static gboolean cycle_expired(PnvSession *s)
{
    return g_get_monotonic_time() > s->cycle_deadline;
}

static void cycle_give_up(PnvSession *s, const char *reason)
{
    s->cycle_active = FALSE;
    pnv_give_up(s, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED, "reconnecting failed within %d s (trigger: %s): %s",
                CYCLE_TIMEOUT_S, s->cycle_trigger, reason);
}

static gboolean cycle_retry(gpointer data)
{
    PnvSession *s = data;
    NMVpnServiceState state = NM_VPN_SERVICE_STATE_UNKNOWN;

    s->cycle_timer = 0;
    g_object_get(s->plugin, NM_VPN_SERVICE_PLUGIN_STATE, &state, NULL);
    /* Only a new STARTING makes NM reread DNS; while still STARTING, setting it again is a no-op. */
    if (state == NM_VPN_SERVICE_STATE_STARTED)
        cycle_step1(s);
    else
        cycle_step2(s);
    return G_SOURCE_REMOVE;
}

static void cycle_failed(PnvSession *s, const char *reason)
{
    guint delay = MIN(1u << MIN(s->cycle_attempt - 1, 4), BACKOFF_MAX_S);

    if (cycle_expired(s)) {
        cycle_give_up(s, reason);
        return;
    }
    pnv_warning(s, "reconnect attempt %u failed (trigger: %s): %s; next in %u s", s->cycle_attempt, s->cycle_trigger,
                reason, delay);
    s->cycle_attempt++;
    s->cycle_timer = g_timeout_add_seconds(delay, cycle_retry, s);
}

static void cycle_checked(PnvSession *s, gboolean ok, const char *reason)
{
    if (!s->cycle_active)
        return;
    if (!ok) {
        cycle_failed(s, reason);
        return;
    }
    if (s->cycle_redo) {
        /* Another device change came in meanwhile: once more with the latest device, no extra STARTING. */
        s->cycle_redo = FALSE;
        cycle_step2(s);
        return;
    }
    s->cycle_active = FALSE;
    s->live_failures = 0;
    remove_temp_block(s);
    pnv_message(s, "reconnected (trigger: %s, attempt %u, parent device %s)", s->cycle_trigger, s->cycle_attempt,
                s->parent ? s->parent : "none");
    s->live_timer = g_timeout_add_seconds(PNV_LIVENESS_INTERVAL, liveness_tick, s);
}

static gboolean cycle_wait_device(gpointer data)
{
    PnvSession *s = data;
    s->cycle_timer = 0;
    cycle_step2(s);
    return G_SOURCE_REMOVE;
}

static void cycle_step2(PnvSession *s)
{
    g_autofree char *parent = current_parent(s);
    g_autoptr(GError) error = NULL;

    if (!parent) {
        if (cycle_expired(s)) {
            cycle_give_up(s, "no parent device came back");
            return;
        }
        /* After resume NM often brings the devices back only later. */
        s->cycle_timer = g_timeout_add_seconds(1, cycle_wait_device, s);
        return;
    }
    g_free(s->parent);
    s->parent = g_steal_pointer(&parent);
    pnv_message(s, "reconnect step 2: recreating %s on %s (trigger: %s, attempt %u)", s->core, s->parent,
                s->cycle_trigger, s->cycle_attempt);
    s->core_ready = FALSE;
    pnv_session_stop_core(s);
    if (!s->protocol->connect(s, &error)) {
        cycle_failed(s, error->message);
        return;
    }
    pnv_session_send_config(s);
    probe(s, TRUE, cycle_checked);
}

static void cycle_step1(PnvSession *s)
{
#ifdef PNV_TEST_HOOKS
    if (pnv_test_hook(s, "skip-starting")) {
        pnv_message(s, "test hook: reconnect without STARTING");
        cycle_step2(s);
        return;
    }
#endif
    /* libnm has no public set_state. NM moves the connection to ACTIVATING and keeps the routes. */
    g_object_set(s->plugin, NM_VPN_SERVICE_PLUGIN_STATE, NM_VPN_SERVICE_STATE_STARTING, NULL);
    pnv_message(s, "reconnect step 1: STARTING (trigger: %s, attempt %u)", s->cycle_trigger, s->cycle_attempt);
    cycle_step2(s);
}

/* Triggers: a parent device change, resume, a core unit failure after success, a dead tunnel. Events during a
 * cycle are coalesced into it. */
void pnv_session_reconnect(PnvSession *s, const char *trigger)
{
    if (s->live_stopped || !s->live_succeeded)
        return;
    if (s->cycle_active) {
        pnv_message(s, "reconnect already running: %s joins it", trigger);
        g_free(s->cycle_trigger);
        s->cycle_trigger = g_strdup(trigger);
        s->cycle_redo = TRUE;
        return;
    }
    s->cycle_active = TRUE;
    s->cycle_count++;
    s->cycle_redo = FALSE;
    s->cycle_attempt = 1;
    s->cycle_deadline = g_get_monotonic_time() + CYCLE_TIMEOUT_S * G_TIME_SPAN_SECOND;
    g_free(s->cycle_trigger);
    s->cycle_trigger = g_strdup(trigger);
    g_clear_handle_id(&s->live_timer, g_source_remove);
    cycle_step1(s);
}

/* NM 1.58.1 leaves a persistent VPN without a parent device once the old one goes, and assigns a new one only when
 * the policy recomputes the best default and the VPN is not it (nm-policy.c, update_ip4_routing). So the daemon
 * makes the VPN step aside for a moment: STARTING, then Config and an Ip4Config with never-default; on ACTIVATED
 * NM recomputes, assigns the device with the best default route and moves the route to the gateway onto it, and
 * the devices change starts the regular cycle. During that moment the default route is not ours, so a temporary
 * block chain (the kill switch rules) keeps traffic from leaving directly, whatever the profile's kill switch. */
static void remove_temp_block(PnvSession *s)
{
    if (s->temp_block)
        pnv_temp_block_remove(s->uuid);
    s->temp_block = FALSE;
}

/* libnm's notify::devices for the assignment may come before its cache has the device, so while stepping
 * aside the daemon also looks every second, until the cycle timeout. */
static gboolean reparent_poll(gpointer data)
{
    PnvSession *s = data;
    g_autofree char *parent = NULL;
    g_autofree char *trigger = NULL;

    if (!s->reparenting) {
        s->pending_timer = 0;
        return G_SOURCE_REMOVE;
    }
    parent = current_parent(s);
    if (parent) {
        s->pending_timer = 0;
        s->reparenting = FALSE;
        trigger = g_strdup_printf("parent device %s assigned by NM", parent);
        pnv_session_reconnect(s, trigger);
        return G_SOURCE_REMOVE;
    }
    if (g_get_monotonic_time() > s->reparent_deadline) {
        s->pending_timer = 0;
        pnv_give_up(s, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED, "no parent device came back within %d s",
                    CYCLE_TIMEOUT_S);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean reparent(gpointer data)
{
    PnvSession *s = data;
    g_autoptr(GError) error = NULL;

    g_autofree char *parent = current_parent(s);

    s->pending_timer = 0;
    if (s->live_stopped)
        return G_SOURCE_REMOVE;
    if (parent) {
        /* NM had a device again by the time the wait was over. */
        g_autofree char *trigger = g_strdup_printf("parent device %s", parent);
        pnv_session_reconnect(s, trigger);
        return G_SOURCE_REMOVE;
    }
    pnv_message(s, "NM left the VPN without a parent device: stepping aside so that it assigns one");
    if (default_route(s)) {
        pnv_session_add_cleanup(s, "temporary block", remove_temp_block);
        if (pnv_temp_block_install(s->uuid, s->profile->allow_lan, &error))
            s->temp_block = TRUE;
        else
            pnv_warning(s, "cannot install the temporary block: %s", error->message);
    }
    s->reparenting = TRUE;
    s->reparent_deadline = g_get_monotonic_time() + CYCLE_TIMEOUT_S * G_TIME_SPAN_SECOND;
    g_clear_handle_id(&s->live_timer, g_source_remove);
#ifdef PNV_TEST_HOOKS
    if (!pnv_test_hook(s, "skip-starting"))
#endif
        g_object_set(s->plugin, NM_VPN_SERVICE_PLUGIN_STATE, NM_VPN_SERVICE_STATE_STARTING, NULL);
    pnv_session_send_config(s);
    s->pending_timer = g_timeout_add_seconds(1, reparent_poll, s);
    return G_SOURCE_REMOVE;
}

static void devices_changed(GObject *ac G_GNUC_UNUSED, GParamSpec *pspec G_GNUC_UNUSED, gpointer data)
{
    PnvSession *s = data;
    g_autofree char *parent = current_parent(s);
    g_autofree char *trigger = NULL;

    if (g_strcmp0(parent, s->parent) == 0)
        return;
    trigger = g_strdup_printf("parent device %s -> %s", s->parent ? s->parent : "none", parent ? parent : "none");
    if (!parent) {
        /* Already stepping aside: the poll waits for the device. A device without its interface name yet
         * counts as none here, and the poll sees it once NM has it. */
        if (s->reparenting)
            return;
        /* Let NM finish taking the old device down first. */
        g_clear_handle_id(&s->pending_timer, g_source_remove);
        s->pending_timer = g_timeout_add_seconds(2, reparent, s);
        return;
    }
    if (s->reparenting) {
        s->reparenting = FALSE;
        g_clear_handle_id(&s->pending_timer, g_source_remove);
    }
    pnv_session_reconnect(s, trigger);
}

/* logind only: anyone else could send PrepareForSleep on the bus. */
static void sleep_changed(GDBusConnection *bus G_GNUC_UNUSED, const char *sender G_GNUC_UNUSED,
                          const char *path G_GNUC_UNUSED, const char *interface G_GNUC_UNUSED,
                          const char *signal G_GNUC_UNUSED, GVariant *params, gpointer data)
{
    gboolean sleeping;

    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(b)")))
        return;
    g_variant_get(params, "(b)", &sleeping);
    if (!sleeping)
        pnv_session_reconnect(data, "resume");
}

static void watch_network(PnvSession *s)
{
    if (s->ac)
        g_signal_connect(s->ac, "notify::" NM_ACTIVE_CONNECTION_DEVICES, G_CALLBACK(devices_changed), s);
    s->sleep_watch = g_dbus_connection_signal_subscribe(pnv_bus(), "org.freedesktop.login1",
                                                        "org.freedesktop.login1.Manager", "PrepareForSleep",
                                                        "/org/freedesktop/login1", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                        sleep_changed, s, NULL);
}

/* Right after ACTIVATED; only its success counts as the session's success. Then every 60 s. */
static void start_liveness(PnvSession *s)
{
    s->live_started = TRUE;
    pnv_session_add_cleanup(s, "liveness check", stop_liveness);
    watch_network(s);
    liveness_run(s);
    s->live_timer = g_timeout_add_seconds(PNV_LIVENESS_INTERVAL, liveness_tick, s);
}

/* NM tells why the VPN goes down (0.7: before Disconnect reaches the daemon); only a user's disconnect and the
 * removal of the profile count as explicit. */
static void activation_changed(NMActiveConnection *ac G_GNUC_UNUSED, guint state, guint reason, gpointer data)
{
    PnvSession *s = data;

    if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED && !s->live_started)
        start_liveness(s);
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
    /* A kill switch left by an earlier session that had succeeded: work under it until this one succeeds. */
    if (pnv_killswitch_present(s->uuid)) {
        s->inherited = TRUE;
        pnv_state_set(s->run_dir, "inherited", TRUE);
        pnv_message(s, "the kill switch of an earlier session is in place: working under it (inherited block)");
        pnv_status_block(s->uuid, "inherited");
    }
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
    if (!s->profile->kill_switch || s->inherited)
        return TRUE;
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
    if (!g_str_equal(state, "failed") && !g_str_equal(state, "inactive") && !g_str_equal(state, "deactivating"))
        return;
    if (s->live_succeeded) {
        g_autofree char *trigger = g_strdup_printf("%s stopped (%s)", s->core, state);
        s->core_ready = FALSE;
        pnv_session_reconnect(s, trigger);
        return;
    }
    pnv_give_up(s, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED, "%s stopped (%s); see journalctl -u %s", s->core, state,
                s->core);
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
    /* broken-config: the core unit fails at once, for any core (amneziawg-go reads no config file). */
    if (pnv_test_hook(s, "broken-config")) {
        g_free(binary);
        binary = g_strdup("/bin/false");
    }
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
    pnv_session_add_cleanup(s, "core unit", pnv_session_stop_core);
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
#ifdef PNV_TEST_HOOKS
    /* cycle-dns: a different DNS list from the cycle shows whether NM applied it (5.8). */
    if (pnv_test_hook(s, "cycle-dns") && s->cycle_active)
        g_variant_builder_add_value(&servers, ip4("203.0.113.53"));
#endif
    g_variant_builder_init(&routes, G_VARIANT_TYPE("aau"));
    for (guint i = 0; i < plan->routes->len; i++) {
        PnvCidr4 c = g_array_index(plan->routes, PnvCidr4, i);
        guint32 route[] = {htonl(c.address), c.prefix, 0, 0};
        g_variant_builder_add_value(&routes, g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, route, 4, sizeof *route));
    }
    /* NM 1.58.1 returns a VPN from STARTING to ACTIVATED only after a commit of a changed IP configuration
     * (nm-vpn-connection.c, POST_COMMIT → PRE_UP), so an identical Ip4Config would leave it ACTIVATING. Every other
     * cycle therefore adds a /32 to another address of the TUN's own subnet, which the subnet route covers anyway. */
    if (s->cycle_count % 2) {
        guint32 mask = s->tun_prefix ? ~0u << (32 - s->tun_prefix) : 0;
        guint32 other = (s->tun_address & mask) + ((s->tun_address & ~mask) == 2 ? 1 : 2);
        guint32 route[] = {htonl(other), 32, 0, 0};
        g_variant_builder_add_value(&routes, g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, route, 4, sizeof *route));
    }
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_ADDRESS, g_variant_new_uint32(htonl(s->tun_address)));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_PREFIX, g_variant_new_uint32(s->tun_prefix));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_DNS, g_variant_builder_end(&servers));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_ROUTES, g_variant_builder_end(&routes));
    if (plan->never_default || s->reparenting)
        g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_NEVER_DEFAULT, g_variant_new_boolean(TRUE));
    nm_vpn_service_plugin_set_ip4_config(s->plugin, g_variant_builder_end(&b));
    pnv_message(s, "Config and Ip4Config sent");
}
