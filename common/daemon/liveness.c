/* SPDX-License-Identifier: MIT */
/* The liveness check (design.md, "Core supervision and cleanup"): a DNS query through the TUN for the proxy
 * cores, the handshake age for AmneziaWG. The scheduling rules are pure functions (unit-tested with a fake
 * clock); the probe itself is asynchronous on the daemon's main loop. */
#include <arpa/inet.h>
#include <errno.h>
#include <glib-unix.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "pnv-daemon.h"

/* A failed periodic check counts; a passed one resets the count. */
PnvLivenessAction pnv_liveness_record(guint *failures, gboolean passed)
{
    if (passed) {
        *failures = 0;
        return PNV_LIVENESS_OK;
    }
    return ++*failures >= PNV_LIVENESS_FAILURES ? PNV_LIVENESS_DEAD : PNV_LIVENESS_FAILED;
}

/* The oldest handshake an AmneziaWG tunnel may have at a periodic check, after the nudge. WireGuard rekeys only
 * when there is traffic, after RekeyAfterTime (120 s); as a responder the client may go up to RejectAfterTime
 * (180 s) without one. So: the profile's RekeyAfterTime (its upper end) or 120, plus 60 for the responder case,
 * plus a 10 s margin. */
guint pnv_awg_age_threshold(guint rekey_after_time)
{
    return MAX(rekey_after_time, 120) + 60 + 10;
}

/* The worst case from a stopped server to the give-up: the age passes the threshold just after a check, the next
 * check sees it, and two more follow 60 s apart. */
guint pnv_awg_detection_bound(guint rekey_after_time)
{
    return pnv_awg_age_threshold(rekey_after_time) + PNV_LIVENESS_INTERVAL * PNV_LIVENESS_FAILURES;
}

/* The DNS probe: "." SOA, any well-formed answer with our ID counts. */
typedef struct {
    PnvSession *session;
    PnvProbeDone done;
    int fd;
    guint16 id;
    guint watch, resend, timeout;
    struct sockaddr_in to;
    guint8 query[17];
} DnsProbe;

static void dns_finish(DnsProbe *p, gboolean ok, const char *reason)
{
    PnvProbeDone done = p->done;
    PnvSession *session = p->session;

    g_clear_handle_id(&p->watch, g_source_remove);
    g_clear_handle_id(&p->resend, g_source_remove);
    g_clear_handle_id(&p->timeout, g_source_remove);
    close(p->fd);
    g_free(p);
    done(session, ok, reason);
}

static gboolean dns_send(gpointer data)
{
    DnsProbe *p = data;
    sendto(p->fd, p->query, sizeof p->query, 0, (struct sockaddr *)&p->to, sizeof p->to);
    return G_SOURCE_CONTINUE;
}

static gboolean dns_reply(int fd, GIOCondition condition G_GNUC_UNUSED, gpointer data)
{
    DnsProbe *p = data;
    guint8 buf[512];
    ssize_t n = recv(fd, buf, sizeof buf, 0);

    /* The header: our ID, QR set. */
    if (n >= 12 && (buf[0] << 8 | buf[1]) == p->id && (buf[2] & 0x80)) {
        p->watch = 0;
        dns_finish(p, TRUE, NULL);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean dns_timeout(gpointer data)
{
    DnsProbe *p = data;
    g_autofree char *reason = g_strdup_printf("no DNS answer from %s through %s", inet_ntoa(p->to.sin_addr),
                                              pnv_session_tun(p->session));
    p->timeout = 0;
    dns_finish(p, FALSE, reason);
    return G_SOURCE_REMOVE;
}

/* The query goes out of the TUN whatever the routes say (SO_BINDTODEVICE), so the guard and the kill switch,
 * which let the tunnel through, let it through too. Resent every 2 s within the timeout. */
void pnv_dns_probe(PnvSession *session, const char *target, guint timeout_s, PnvProbeDone done)
{
    static const guint8 question[] = {0, 0, 6, 0, 1}; /* root name, type SOA, class IN */
    DnsProbe *p = g_new0(DnsProbe, 1);
    const char *tun = pnv_session_tun(session);

    p->session = session;
    p->done = done;
    p->id = g_random_int_range(1, 65536);
    p->to = (struct sockaddr_in){.sin_family = AF_INET, .sin_port = htons(53)};
    inet_pton(AF_INET, target, &p->to.sin_addr);
    p->query[0] = p->id >> 8;
    p->query[1] = p->id & 0xff;
    p->query[2] = 0x01; /* RD */
    p->query[5] = 1;    /* QDCOUNT */
    memcpy(p->query + 12, question, sizeof question);
    p->fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (p->fd < 0 || setsockopt(p->fd, SOL_SOCKET, SO_BINDTODEVICE, tun, strlen(tun)) != 0) {
        g_autofree char *reason = g_strdup_printf("cannot bind a socket to %s: %s", tun, g_strerror(errno));
        if (p->fd >= 0)
            close(p->fd);
        g_free(p);
        done(session, FALSE, reason);
        return;
    }
    p->watch = g_unix_fd_add(p->fd, G_IO_IN, dns_reply, p);
    p->resend = g_timeout_add_seconds(2, dns_send, p);
    p->timeout = g_timeout_add_seconds(timeout_s, dns_timeout, p);
    dns_send(p);
}
