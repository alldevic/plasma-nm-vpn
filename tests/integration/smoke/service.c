/* Minimal NM VPN plugin daemon for milestone 0.7: hysteria2 through sing-box on a daemon-owned TUN. */
#include <NetworkManager.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <linux/if_tun.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SERVICE   "org.freedesktop.NetworkManager.pnv-proto"
#define SINGBOX   "/opt/pnv-proto/bin/sing-box"
#define GUARD     "/usr/local/lib/pnv-proto/guard-cleanup"
#define LOGFILE   "/run/pnv-proto.log"
#define CORE_USER "plasma-nm-vpn"
#define TUN_ADDR  "198.18.0.1"
#define TUN_MTU   1400

typedef struct {
    NMVpnServicePlugin parent;
} PnvPlugin;
typedef struct {
    NMVpnServicePluginClass parent;
} PnvPluginClass;

G_DEFINE_TYPE(PnvPlugin, pnv_plugin, NM_TYPE_VPN_SERVICE_PLUGIN)

static NMVpnServicePlugin *plugin;
static GDBusConnection *sysbus;
static NMClient *client;
static char *ac_path; /* our own active connection */
static char *uuid, *server, *port, *password, *obfs, *ca;
static char tun[IFNAMSIZ], rundir[128], scope[128], guard[128], core[128];

static void plog(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void plog(const char *fmt, ...)
{
    struct timespec ts;
    char stamp[16];
    g_autofree char *msg = NULL;
    va_list ap;
    FILE *f;

    clock_gettime(CLOCK_REALTIME, &ts);
    strftime(stamp, sizeof stamp, "%T", localtime(&ts.tv_sec));
    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    fprintf(stderr, "pnv-proto[%d] %s.%03ld %s\n", getpid(), stamp, ts.tv_nsec / 1000000, msg);
    if ((f = fopen(LOGFILE, "a"))) {
        fprintf(f, "pnv-proto[%d] %s.%03ld %s\n", getpid(), stamp, ts.tv_nsec / 1000000, msg);
        fclose(f);
    }
}

/* systemd over D-Bus */

static GVariant *manager_call(const char *method, GVariant *args, GError **error)
{
    return g_dbus_connection_call_sync(sysbus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager", method, args, NULL,
                                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);
}

static gboolean start_unit(const char *name, GVariantBuilder *props)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) r = manager_call("StartTransientUnit",
                                         g_variant_new("(ssa(sv)a(sa(sv)))", name, "fail", props, NULL), &error);
    if (!r)
        plog("StartTransientUnit %s: %s", name, error->message);
    return r != NULL;
}

static void stop_unit(const char *name)
{
    g_autoptr(GVariant) r = manager_call("StopUnit", g_variant_new("(ss)", name, "replace"), NULL);
}

static char *unit_state(const char *name)
{
    g_autoptr(GVariant) r = manager_call("GetUnit", g_variant_new("(s)", name), NULL);
    g_autoptr(GVariant) v = NULL, s = NULL;
    const char *path;

    if (!r)
        return g_strdup("gone");
    g_variant_get(r, "(&o)", &path);
    v = g_dbus_connection_call_sync(sysbus, "org.freedesktop.systemd1", path, "org.freedesktop.DBus.Properties",
                                    "Get", g_variant_new("(ss)", "org.freedesktop.systemd1.Unit", "ActiveState"),
                                    G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    if (!v)
        return g_strdup("gone");
    g_variant_get(v, "(v)", &s);
    return g_variant_dup_string(s, NULL);
}

static gboolean unit_down(const char *name)
{
    g_autofree char *s = unit_state(name);
    return g_str_equal(s, "inactive") || g_str_equal(s, "failed") || g_str_equal(s, "gone");
}

static gboolean wait_down(const char *name, int timeout_ms)
{
    for (int t = 0; t < timeout_ms; t += 100) {
        if (unit_down(name))
            return TRUE;
        g_usleep(100000);
    }
    return FALSE;
}

static void prop(GVariantBuilder *b, const char *name, GVariant *v)
{
    g_variant_builder_add(b, "(sv)", name, v);
}

static GVariant *strv(const char *a, const char *b)
{
    const char *v[] = {a, b, NULL};
    return g_variant_new_strv(v, -1);
}

static GVariant *command(const char *const *argv)
{
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a(sasb)"));
    g_variant_builder_add(&b, "(s^asb)", argv[0], argv, FALSE);
    return g_variant_builder_end(&b);
}

/* TUN and rtnetlink */

static gboolean tun_create(void)
{
    struct ifreq ifr = {.ifr_flags = IFF_TUN | IFF_NO_PI};
    struct passwd *pw = getpwnam(CORE_USER);
    gboolean ok;
    int fd, s;

    if (!pw || (fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC)) < 0)
        return FALSE;
    g_strlcpy(ifr.ifr_name, tun, IFNAMSIZ);
    ok = ioctl(fd, TUNSETIFF, &ifr) == 0 && ioctl(fd, TUNSETOWNER, pw->pw_uid) == 0 && ioctl(fd, TUNSETPERSIST, 1) == 0;
    close(fd);
    s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    ifr.ifr_mtu = TUN_MTU;
    ok = ok && ioctl(s, SIOCSIFMTU, &ifr) == 0;
    close(s);
    return ok;
}

static void tun_delete(void)
{
    struct ifreq ifr = {.ifr_flags = IFF_TUN | IFF_NO_PI};
    int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);

    if (fd < 0)
        return;
    g_strlcpy(ifr.ifr_name, tun, IFNAMSIZ);
    if (ioctl(fd, TUNSETIFF, &ifr) == 0)
        ioctl(fd, TUNSETPERSIST, 0);
    close(fd);
}

/* IFLA_CARRIER via RTM_GETLINK: 1 once the core has the TUN open, whether or not the link is up (0.4). */
static int tun_carrier(void)
{
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
    } req = {
        .nh = {.nlmsg_len = sizeof req, .nlmsg_type = RTM_GETLINK, .nlmsg_flags = NLM_F_REQUEST},
        .ifi = {.ifi_family = AF_UNSPEC, .ifi_index = (int) if_nametoindex(tun)},
    };
    char buf[8192];
    int fd, carrier = -1;
    ssize_t n;

    if (!req.ifi.ifi_index || (fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE)) < 0)
        return -1;
    if (send(fd, &req, sizeof req, 0) > 0 && (n = recv(fd, buf, sizeof buf, 0)) > 0) {
        struct nlmsghdr *nh = (struct nlmsghdr *) buf;
        if (NLMSG_OK(nh, (size_t) n) && nh->nlmsg_type == RTM_NEWLINK) {
            int len = IFLA_PAYLOAD(nh);
            for (struct rtattr *a = IFLA_RTA(NLMSG_DATA(nh)); RTA_OK(a, len); a = RTA_NEXT(a, len))
                if (a->rta_type == IFLA_CARRIER)
                    carrier = *(unsigned char *) RTA_DATA(a);
        }
    }
    close(fd);
    return carrier;
}

static gboolean wait_attached(void)
{
    for (int t = 0; t < 15000; t += 100) {
        if (tun_carrier() == 1)
            return TRUE;
        g_usleep(100000);
    }
    return FALSE;
}

/* The core */

static gboolean write_config(void)
{
    g_autofree char *path = g_build_filename(rundir, "config.json", NULL);
    g_autofree char *json = g_strdup_printf(
        "{\"log\": {\"level\": \"info\"},\n"
        " \"inbounds\": [{\"type\": \"tun\", \"interface_name\": \"%s\", \"address\": [\"%s/30\"], \"mtu\": %d,\n"
        "   \"auto_route\": false, \"stack\": \"system\"}],\n"
        " \"outbounds\": [{\"type\": \"hysteria2\", \"tag\": \"proxy\", \"server\": \"%s\", \"server_port\": %s,\n"
        "   \"password\": \"%s\", \"obfs\": {\"type\": \"salamander\", \"password\": \"%s\"},\n"
        "   \"tls\": {\"enabled\": true, \"server_name\": \"srv.stand.test\", \"alpn\": [\"h3\"], \"certificate_path\": \"%s\"},\n"
        "   \"bind_interface\": \"stand-cli\"}],\n"
        " \"route\": {\"final\": \"proxy\"}}\n",
        tun, TUN_ADDR, TUN_MTU, server, port, password, obfs, ca);
    return g_file_set_contents_full(path, json, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
}

static gboolean start_core(void)
{
    g_autofree char *cred = g_build_filename(rundir, "config.json", NULL);
    const char *argv[] = {SINGBOX, "run", "-c", "${CREDENTIALS_DIRECTORY}/config.json", NULL};
    guint64 cap = 1ULL << 12; /* CAP_NET_ADMIN */
    GVariantBuilder p, c;

    g_variant_builder_init(&c, G_VARIANT_TYPE("a(ss)"));
    g_variant_builder_add(&c, "(ss)", "config.json", cred);
    g_variant_builder_init(&p, G_VARIANT_TYPE("a(sv)"));
    prop(&p, "Description", g_variant_new_string("pnv-proto core"));
    prop(&p, "ExecStart", command(argv));
    prop(&p, "LoadCredential", g_variant_builder_end(&c));
    prop(&p, "User", g_variant_new_string(CORE_USER));
    prop(&p, "AmbientCapabilities", g_variant_new_uint64(cap));
    prop(&p, "CapabilityBoundingSet", g_variant_new_uint64(cap));
    prop(&p, "ProtectSystem", g_variant_new_string("strict"));
    prop(&p, "ProtectHome", g_variant_new_string("yes"));
    prop(&p, "PrivateTmp", g_variant_new_boolean(TRUE));
    prop(&p, "NoNewPrivileges", g_variant_new_boolean(TRUE));
    prop(&p, "KillSignal", g_variant_new_int32(9));
    prop(&p, "BindsTo", strv(scope, NULL));
    prop(&p, "After", strv(scope, guard));
    prop(&p, "CollectMode", g_variant_new_string("inactive-or-failed"));
    if (!start_unit(core, &p))
        return FALSE;
    if (!wait_attached()) {
        plog("the core did not attach to %s within 15 s", tun);
        return FALSE;
    }
    plog("core attached to %s (ifindex %u)", tun, if_nametoindex(tun));
    return TRUE;
}

static void send_config(gboolean second_dns)
{
    GVariantBuilder b, dns;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_TUNDEV, g_variant_new_string(tun));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY, g_variant_new_uint32(inet_addr(server)));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP4, g_variant_new_boolean(TRUE));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CAN_PERSIST, g_variant_new_boolean(TRUE));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_CONFIG_MTU, g_variant_new_uint32(TUN_MTU));
    nm_vpn_service_plugin_set_config(plugin, g_variant_builder_end(&b));

    g_variant_builder_init(&dns, G_VARIANT_TYPE("au"));
    g_variant_builder_add(&dns, "u", inet_addr("203.0.113.1"));
    if (second_dns)
        g_variant_builder_add(&dns, "u", inet_addr("1.1.1.1"));
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_ADDRESS, g_variant_new_uint32(inet_addr(TUN_ADDR)));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_PREFIX, g_variant_new_uint32(30));
    g_variant_builder_add(&b, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_DNS, g_variant_builder_end(&dns));
    nm_vpn_service_plugin_set_ip4_config(plugin, g_variant_builder_end(&b));
    plog("Config and Ip4Config sent (dns: %s)", second_dns ? "203.0.113.1, 1.1.1.1" : "203.0.113.1");
}

/* Re-activation signals and deactivation reasons */

static const char *ac_state_name(guint s)
{
    static const char *const n[] = {"unknown", "activating", "activated", "deactivating", "deactivated"};
    return s < G_N_ELEMENTS(n) ? n[s] : "?";
}

/* Another activation of our profile: from libnm's cache and from a fresh round trip to NM. */
static void probe_reactivation(const char *where)
{
    GString *cached = g_string_new(NULL), *fresh = g_string_new(NULL);
    const GPtrArray *acs = nm_client_get_active_connections(client);
    g_autoptr(GVariant) r = NULL, v = NULL;
    g_autofree char *marker = g_build_filename(rundir, "takeover", NULL);

    for (guint i = 0; acs && i < acs->len; i++) {
        NMActiveConnection *ac = acs->pdata[i];
        const char *path = nm_object_get_path(NM_OBJECT(ac));
        if (g_strcmp0(nm_active_connection_get_uuid(ac), uuid) == 0 && g_strcmp0(path, ac_path) != 0)
            g_string_append_printf(cached, "%s=%s ", strrchr(path, '/') + 1,
                                   ac_state_name(nm_active_connection_get_state(ac)));
    }
    r = g_dbus_connection_call_sync(sysbus, NM_DBUS_SERVICE, NM_DBUS_PATH, "org.freedesktop.DBus.Properties", "Get",
                                    g_variant_new("(ss)", NM_DBUS_INTERFACE, "ActiveConnections"),
                                    G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    if (r) {
        g_autoptr(GVariantIter) it = NULL;
        const char *path;
        g_variant_get(r, "(v)", &v);
        g_variant_get(v, "ao", &it);
        while (g_variant_iter_next(it, "&o", &path)) {
            g_autoptr(GVariant) all = NULL;
            const char *u = NULL;
            guint32 st = 0;
            if (g_strcmp0(path, ac_path) == 0)
                continue;
            all = g_dbus_connection_call_sync(sysbus, NM_DBUS_SERVICE, path, "org.freedesktop.DBus.Properties",
                                              "GetAll", g_variant_new("(s)", NM_DBUS_INTERFACE_ACTIVE_CONNECTION),
                                              G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
            if (!all)
                continue;
            g_autoptr(GVariant) d = g_variant_get_child_value(all, 0);
            g_variant_lookup(d, "Uuid", "&s", &u);
            g_variant_lookup(d, "State", "u", &st);
            if (g_strcmp0(u, uuid) == 0)
                g_string_append_printf(fresh, "%s=%s ", strrchr(path, '/') + 1, ac_state_name(st));
        }
    }
    plog("probe at %s: other activation (cache) [%s] (fresh) [%s], takeover marker %s", where,
         cached->str, fresh->str, g_file_test(marker, G_FILE_TEST_EXISTS) ? "PRESENT" : "absent");
    g_string_free(cached, TRUE);
    g_string_free(fresh, TRUE);
}

static void ac_state_changed(NMActiveConnection *ac, guint state, guint reason, gpointer data)
{
    g_autofree char *r = nm_utils_enum_to_str(NM_TYPE_ACTIVE_CONNECTION_STATE_REASON, reason);
    plog("active connection %s: %s, reason %s", strrchr(ac_path, '/') + 1, ac_state_name(state), r);
    if (state >= NM_ACTIVE_CONNECTION_STATE_DEACTIVATING)
        probe_reactivation("state-changed");
}

static void watch_own_ac(void)
{
    const GPtrArray *acs = nm_client_get_active_connections(client);
    NMActiveConnection *own = NULL;

    /* The newest activation of our profile is ours. */
    for (guint i = 0; acs && i < acs->len; i++) {
        NMActiveConnection *ac = acs->pdata[i];
        if (g_strcmp0(nm_active_connection_get_uuid(ac), uuid) == 0 &&
            (!own || strcmp(nm_object_get_path(NM_OBJECT(ac)), nm_object_get_path(NM_OBJECT(own))) > 0))
            own = ac;
    }
    if (!own) {
        plog("own active connection not found in libnm's cache");
        return;
    }
    ac_path = g_strdup(nm_object_get_path(NM_OBJECT(own)));
    g_signal_connect(own, "state-changed", G_CALLBACK(ac_state_changed), NULL);
    plog("watching active connection %s", strrchr(ac_path, '/') + 1);
}

/* Connect, disconnect, reconnect */

static void cleanup(void)
{
    stop_unit(core);
    if (!wait_down(core, 10000))
        plog("core unit still up after 10 s");
    tun_delete();
    g_autofree char *cfg = g_build_filename(rundir, "config.json", NULL);
    g_autofree char *marker = g_build_filename(rundir, "takeover", NULL);
    g_unlink(cfg);
    g_unlink(marker);
    g_rmdir(rundir);
    stop_unit(guard);
}

static gboolean do_connect(gpointer data)
{
    const char *guard_argv[] = {GUARD, "--daemon-gone", uuid, NULL};
    const char *const units[] = {scope, guard, core};
    GVariantBuilder p;
    gint64 t0 = g_get_monotonic_time();

    /* 1. Orphan cleanup: mark a takeover if the previous instance is alive, then wait for it. */
    if (!unit_down(scope)) {
        g_autofree char *marker = g_build_filename(rundir, "takeover", NULL);
        g_mkdir_with_parents(rundir, 0700);
        g_file_set_contents(marker, "", 0, NULL);
        plog("previous instance alive: takeover marker placed");
    }
    for (guint i = 0; i < G_N_ELEMENTS(units); i++)
        if (!wait_down(units[i], 10000)) {
            plog("%s still up after 10 s", units[i]);
            goto fail;
        }
    plog("orphan wait %" G_GINT64_FORMAT " ms", (g_get_monotonic_time() - t0) / 1000);

    /* 2. Run directory, scope, guard unit. */
    if (g_mkdir_with_parents(rundir, 0700) != 0)
        goto fail;
    g_variant_builder_init(&p, G_VARIANT_TYPE("a(sv)"));
    prop(&p, "Description", g_variant_new_string("pnv-proto daemon"));
    prop(&p, "PIDs", g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, &(guint32){getpid()}, 1, sizeof(guint32)));
    prop(&p, "CollectMode", g_variant_new_string("inactive-or-failed"));
    if (!start_unit(scope, &p))
        goto fail;
    g_variant_builder_init(&p, G_VARIANT_TYPE("a(sv)"));
    prop(&p, "Description", g_variant_new_string("pnv-proto guard"));
    prop(&p, "Type", g_variant_new_string("oneshot"));
    prop(&p, "RemainAfterExit", g_variant_new_boolean(TRUE));
    prop(&p, "ExecStop", command(guard_argv));
    prop(&p, "BindsTo", strv(scope, NULL));
    prop(&p, "After", strv(scope, NULL));
    prop(&p, "CollectMode", g_variant_new_string("inactive-or-failed"));
    if (!start_unit(guard, &p))
        goto fail;

    /* 3. TUN, config, core. */
    if (!tun_create() || !write_config() || !start_core())
        goto fail;
    send_config(FALSE);
    watch_own_ac();
    return G_SOURCE_REMOVE;

fail:
    plog("connect failed");
    cleanup();
    nm_vpn_service_plugin_failure(plugin, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
    return G_SOURCE_REMOVE;
}

static gboolean real_connect(NMVpnServicePlugin *p, NMConnection *connection, GError **error)
{
    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);

    uuid = g_strdup(nm_connection_get_uuid(connection));
    server = g_strdup(nm_setting_vpn_get_data_item(s_vpn, "server"));
    port = g_strdup(nm_setting_vpn_get_data_item(s_vpn, "port"));
    obfs = g_strdup(nm_setting_vpn_get_data_item(s_vpn, "obfs"));
    ca = g_strdup(nm_setting_vpn_get_data_item(s_vpn, "ca"));
    password = g_strdup(nm_setting_vpn_get_secret(s_vpn, "password"));
    if (!server || !port || !obfs || !ca || !password) {
        g_set_error_literal(error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS, "missing server, port, obfs, ca or password");
        return FALSE;
    }
    g_snprintf(tun, sizeof tun, "pnv-%.8s", uuid);
    g_snprintf(rundir, sizeof rundir, "/run/plasma-nm-vpn/%s", uuid);
    g_snprintf(scope, sizeof scope, "plasma-nm-vpn-daemon-%s.scope", uuid);
    g_snprintf(guard, sizeof guard, "plasma-nm-vpn-guard-%s.service", uuid);
    g_snprintf(core, sizeof core, "plasma-nm-vpn-core-%s.service", uuid);
    plog("Connect %s", uuid);
    g_idle_add(do_connect, NULL);
    return TRUE;
}

static gboolean real_need_secrets(NMVpnServicePlugin *p, NMConnection *connection, const char **setting_name,
                                  GError **error)
{
    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);

    if (nm_setting_vpn_get_secret(s_vpn, "password"))
        return FALSE;
    *setting_name = NM_SETTING_VPN_SETTING_NAME;
    return TRUE;
}

static gboolean real_disconnect(NMVpnServicePlugin *p, GError **error)
{
    plog("Disconnect: start");
    if (client && ac_path)
        probe_reactivation("disconnect");
    cleanup();
    plog("Disconnect: handler done");
    return TRUE;
}

static gboolean reconnect(gpointer data)
{
    static gboolean second_dns;
    gboolean starting = GPOINTER_TO_INT(data);

    plog("reconnect cycle %s STARTING, TUN ifindex %u", starting ? "with" : "without", if_nametoindex(tun));
    if (starting)
        g_object_set(plugin, NM_VPN_SERVICE_PLUGIN_STATE, NM_VPN_SERVICE_STATE_STARTING, NULL);
    stop_unit(core);
    wait_down(core, 10000);
    if (!write_config() || !start_core()) {
        nm_vpn_service_plugin_failure(plugin, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
        return G_SOURCE_CONTINUE;
    }
    second_dns = !second_dns; /* a different DNS list each cycle shows whether NM applied it */
    send_config(second_dns);
    return G_SOURCE_CONTINUE;
}

static void pnv_plugin_init(PnvPlugin *self)
{
}

static void pnv_plugin_class_init(PnvPluginClass *klass)
{
    NMVpnServicePluginClass *parent = NM_VPN_SERVICE_PLUGIN_CLASS(klass);

    parent->connect = real_connect;
    parent->need_secrets = real_need_secrets;
    parent->disconnect = real_disconnect;
}

static void on_quit(NMVpnServicePlugin *p, gpointer loop)
{
    plog("quit signal from libnm");
    g_main_loop_quit(loop);
}

int main(int argc, char **argv)
{
    const char *bus_name = SERVICE;
    g_autoptr(GError) error = NULL;
    GMainLoop *loop;

    for (int i = 1; i + 1 < argc; i++)
        if (g_str_equal(argv[i], "--bus-name"))
            bus_name = argv[i + 1];
    plog("start, bus name %s", bus_name);

    sysbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (sysbus)
        client = nm_client_new(NULL, &error);
    if (client)
        plugin = g_initable_new(pnv_plugin_get_type(), NULL, &error, NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME,
                                bus_name, NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, TRUE, NULL);
    if (!plugin) {
        plog("init failed: %s", error->message);
        return 1;
    }
    loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(plugin, "quit", G_CALLBACK(on_quit), loop);
    g_unix_signal_add(SIGUSR1, reconnect, GINT_TO_POINTER(TRUE));
    g_unix_signal_add(SIGUSR2, reconnect, GINT_TO_POINTER(FALSE));
    g_main_loop_run(loop);
    plog("exit");
    return 0;
}
