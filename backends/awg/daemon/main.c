/* SPDX-License-Identifier: MIT */
/* The daemon for AmneziaWG: amneziawg-go in the core unit, configured over UAPI with `awg setconf`. */
#include <arpa/inet.h>
#include <glib/gstdio.h>
#include <linux/capability.h>
#include <net/if.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "pnv-daemon.h"

#define DATA_DIR "/usr/share/plasma-nm-vpn/amneziawg"
#define AWG PNV_CORE_DIR "/awg"
#define SOCKET_WAIT_MS 15000

static const char *const read_write[] = {"/run/amneziawg", NULL};

static gboolean awg_tun_address(PnvSession *session, guint32 *address, guint *prefix)
{
    const GArray *list = pnv_session_profile(session)->amneziawg.address;
    PnvCidr4 first;

    if (!list || !list->len)
        return FALSE;
    first = g_array_index(list, PnvCidr4, 0);
    *address = first.address;
    *prefix = first.prefix;
    if (list->len > 1)
        pnv_warning(session, "address: using the first IPv4 address, skipping %u more", list->len - 1);
    if (pnv_session_profile(session)->amneziawg.skipped_ipv6)
        pnv_warning(session, "skipping %u IPv6 entries in address and allowed-ips (IPv6 in the tunnel is phase 2)",
                    pnv_session_profile(session)->amneziawg.skipped_ipv6);
    return TRUE;
}

static const GArray *awg_routes(PnvSession *session)
{
    return pnv_session_profile(session)->amneziawg.allowed_ips;
}

static void remove_socket(PnvSession *session)
{
    g_autofree char *sock = pnv_uapi_socket(pnv_session_tun(session));
    g_unlink(sock);
}

/* The latest handshake of the (single) peer, seconds since the epoch; 0 for none, -1 if awg fails. */
static gint64 pnv_awg_latest_handshake(const char *tun)
{
    const char *argv[] = {AWG, "show", tun, "latest-handshakes", NULL};
    g_autofree char *out = NULL;
    const char *tab;
    int status;

    if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL) || !(tab = strchr(out, '\t')))
        return -1;
    return g_ascii_strtoll(tab + 1, NULL, 10);
}

/* A datagram into the tunnel makes amneziawg-go start a handshake when nothing else does (no keepalive, no
 * traffic yet). It goes to the first address of the TUN subnet, port 9 (discard); its content is irrelevant. */
static gboolean pnv_awg_nudge(PnvSession *session)
{
    const char *tun = pnv_session_tun(session);
    const PnvCidr4 first = g_array_index(pnv_session_profile(session)->amneziawg.address, PnvCidr4, 0);
    guint32 mask = first.prefix ? ~0u << (32 - first.prefix) : 0;
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(9),
                             .sin_addr.s_addr = htonl((first.address & mask) + 1)};
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    gboolean ok;

    if (to.sin_addr.s_addr == htonl(first.address))
        to.sin_addr.s_addr = htonl((first.address & mask) + 2);
    ok = fd >= 0 && setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, tun, strlen(tun)) == 0 &&
         sendto(fd, "", 1, 0, (struct sockaddr *)&to, sizeof to) == 1;
    if (fd >= 0)
        close(fd);
    return ok;
}

/* The liveness probe (5.4). The first check waits for the first handshake, nudging after 2 s; a periodic one
 * nudges (WireGuard renews handshakes only with traffic), waits 5 s and compares the handshake age with the
 * threshold from the profile's RekeyAfterTime. */
typedef struct {
    PnvSession *session;
    PnvProbeDone done;
    gboolean first, nudged;
    guint ticks, timeout_s;
} Probe;

static guint rekey_after_time(PnvSession *session)
{
    const GPtrArray *params = pnv_session_profile(session)->amneziawg.params;

    for (guint i = 0; params && i < params->len; i++) {
        const PnvAwgParam *param = params->pdata[i];
        if (g_str_equal(param->key, "RekeyAfterTime")) {
            const char *dash = strchr(param->value, '-');
            return g_ascii_strtoull(dash ? dash + 1 : param->value, NULL, 10);
        }
    }
    return 120;
}

static gboolean probe_tick(gpointer data)
{
    Probe *p = data;
    const char *tun = pnv_session_tun(p->session);
    gint64 latest = pnv_awg_latest_handshake(tun);
    g_autofree char *reason = NULL;

    if (p->first) {
        if (latest > 0) {
            pnv_message(p->session, "first handshake after %u s%s", p->ticks, p->nudged ? " (nudged)" : "");
            p->done(p->session, TRUE, NULL);
            g_free(p);
            return G_SOURCE_REMOVE;
        }
        if (!p->nudged && p->ticks >= 2 && if_nametoindex(tun) != 0)
            p->nudged = pnv_awg_nudge(p->session);
        if (++p->ticks <= p->timeout_s)
            return G_SOURCE_CONTINUE;
        reason = g_strdup_printf("no handshake within %u s", p->timeout_s);
    } else {
        guint threshold = pnv_awg_age_threshold(rekey_after_time(p->session));
        gint64 age = latest > 0 ? g_get_real_time() / G_USEC_PER_SEC - latest : -1;

        if (latest > 0 && age <= threshold) {
            p->done(p->session, TRUE, NULL);
            g_free(p);
            return G_SOURCE_REMOVE;
        }
        reason = latest > 0 ? g_strdup_printf("the last handshake is %" G_GINT64_FORMAT " s old (threshold %u s)", age,
                                              threshold)
                            : g_strdup("no handshake at all");
    }
    p->done(p->session, FALSE, reason);
    g_free(p);
    return G_SOURCE_REMOVE;
}

static void awg_probe(PnvSession *session, gboolean first, guint timeout_s, PnvProbeDone done)
{
    Probe *p = g_new0(Probe, 1);

    *p = (Probe){session, done, first, FALSE, 0, timeout_s};
    if (first) {
        g_timeout_add_seconds(1, probe_tick, p);
        return;
    }
    p->nudged = pnv_awg_nudge(session);
    g_timeout_add_seconds(5, probe_tick, p);
}

static gboolean setconf(PnvSession *session, GError **error)
{
    const PnvProfile *profile = pnv_session_profile(session);
    g_autofree char *config = pnv_awg_config(profile, pnv_session_server_ip(session));
    g_autoptr(GSubprocess) awg = NULL;
    g_autofree char *err = NULL;
    gboolean ok;

    /* The config with the private key goes through a pipe, never to disk. */
    awg = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                               G_SUBPROCESS_FLAGS_STDERR_PIPE,
                           error, AWG, "setconf", pnv_session_tun(session), "/dev/stdin", NULL);
    ok = awg && g_subprocess_communicate_utf8(awg, config, NULL, NULL, &err, error);
    explicit_bzero(config, strlen(config));
    if (ok && !g_subprocess_get_successful(awg)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "awg setconf: %s", err ? g_strstrip(err) : "failed");
        ok = FALSE;
    }
    return ok;
}

/* The post-start step, one function so that the reconnect cycle (5.5) can run it again: no stale socket, the
 * core unit attached to the TUN, its UAPI socket, then the config. */
static gboolean awg_connect(PnvSession *session, GError **error)
{
    const char *tun = pnv_session_tun(session);
    const char *args[] = {"-f", tun, NULL};
    PnvCore core = {
        .binary = "amneziawg-go",
        .args = args,
        .capabilities = 1ULL << CAP_NET_ADMIN,
        .read_write_paths = read_write,
    };
    g_autofree char *sock = pnv_uapi_socket(tun);

    remove_socket(session);
    pnv_session_add_cleanup(session, "UAPI socket", remove_socket);
    if (!pnv_session_start_core(session, &core, NULL, error))
        return FALSE;
    for (guint t = 0; !g_file_test(sock, G_FILE_TEST_EXISTS); t += 100) {
        if (t >= SOCKET_WAIT_MS) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "%s did not appear within %d s", sock,
                        SOCKET_WAIT_MS / 1000);
            return FALSE;
        }
        g_usleep(100 * 1000);
    }
    return setconf(session, error);
}

static const PnvProtocol amneziawg = {
    .service = PNV_SERVICE_AMNEZIAWG,
    .name = "amneziawg",
    .lists = (const PnvListFile[]){{"awg/", DATA_DIR "/amneziawg-params.list"}, {NULL}},
    .default_mtu = 1420,
    .tun_address = awg_tun_address,
    .routes = awg_routes,
    .probe = awg_probe,
    .connect = awg_connect,
};

static const PnvProtocol *const protocols[] = {&amneziawg, NULL};

int main(int argc, char **argv)
{
    return pnv_daemon_main(argc, argv, protocols);
}
