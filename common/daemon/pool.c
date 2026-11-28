/* SPDX-License-Identifier: MIT */
/* The TUN subnet (design.md, "TUN addressing"): a free /30 from the pool, chosen under a lock and reserved per
 * UUID in its run directory, so that two daemons connecting at once never pick the same one. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/rtnetlink.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <net/if.h>
#include <unistd.h>

#include "pnv-daemon.h"

#define DEFAULT_POOL "198.18.0.0/15"

static gboolean overlaps(guint32 a, guint pa, guint32 b, guint pb)
{
    guint p = MIN(pa, pb);
    guint32 mask = p ? ~0u << (32 - p) : 0;
    return ((a ^ b) & mask) == 0;
}

/* IPv4 addresses and route destinations on the machine (all tables), as PnvCidr4. */
static void dump(GArray *used, int type)
{
    struct {
        struct nlmsghdr nh;
        struct rtmsg rt;
    } req = {
        .nh = {.nlmsg_len = sizeof req, .nlmsg_type = type, .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP},
        .rt = {.rtm_family = AF_INET, .rtm_table = RT_TABLE_UNSPEC},
    };
    char buf[32768];
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    gboolean done = FALSE;

    if (fd < 0 || send(fd, &req, sizeof req, 0) < 0)
        goto out;
    while (!done) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0)
            break;
        for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, (size_t)n); nh = NLMSG_NEXT(nh, n)) {
            struct rtattr *a;
            int len;
            guint prefix;

            if (nh->nlmsg_type == NLMSG_DONE || nh->nlmsg_type == NLMSG_ERROR) {
                done = TRUE;
                break;
            }
            if (type == RTM_GETADDR) {
                struct ifaddrmsg *ifa = NLMSG_DATA(nh);
                a = IFA_RTA(ifa);
                len = IFA_PAYLOAD(nh);
                prefix = ifa->ifa_prefixlen;
            } else {
                struct rtmsg *rt = NLMSG_DATA(nh);
                a = RTM_RTA(rt);
                len = RTM_PAYLOAD(nh);
                prefix = rt->rtm_dst_len;
                if (prefix == 0)
                    continue; /* a default route overlaps everything and says nothing */
            }
            for (; RTA_OK(a, len); a = RTA_NEXT(a, len))
                if ((type == RTM_GETADDR && a->rta_type == IFA_LOCAL) || (type == RTM_GETROUTE && a->rta_type == RTA_DST)) {
                    PnvCidr4 c = {ntohl(*(guint32 *)RTA_DATA(a)), prefix};
                    g_array_append_val(used, c);
                }
        }
    }
out:
    if (fd >= 0)
        close(fd);
}

static PnvCidr4 pool(void)
{
    g_autoptr(GKeyFile) conf = g_key_file_new();
    g_autofree char *value = NULL;
    g_autoptr(GArray) list = NULL;
    PnvCidr4 fallback;

    pnv_parse_cidr4_list("pool", DEFAULT_POOL, 1, &list, NULL);
    fallback = g_array_index(list, PnvCidr4, 0);
    g_clear_pointer(&list, g_array_unref);
    if (!g_key_file_load_from_file(conf, PNV_CONF, G_KEY_FILE_NONE, NULL) ||
        !(value = g_key_file_get_string(conf, "tun", "pool", NULL)))
        return fallback;
    if (!pnv_parse_cidr4_list("pool", value, 1, &list, NULL) || list->len != 1 ||
        g_array_index(list, PnvCidr4, 0).prefix > 30) {
        g_warning("%s: [tun] pool=%s is not one IPv4 subnet of /30 or larger; using %s", PNV_CONF, value, DEFAULT_POOL);
        return fallback;
    }
    return g_array_index(list, PnvCidr4, 0);
}

static gboolean read_reservation(const char *path, PnvCidr4 *out)
{
    g_autofree char *text = NULL;
    g_autoptr(GArray) list = NULL;

    if (!g_file_get_contents(path, &text, NULL, NULL) ||
        !pnv_parse_cidr4_list("subnet", g_strstrip(text), 1, &list, NULL) || list->len != 1)
        return FALSE;
    *out = g_array_index(list, PnvCidr4, 0);
    return TRUE;
}

/* Reservations of the other profiles: their run directories' subnet files. */
static void reserved(GArray *used, const char *uuid)
{
    g_autoptr(GDir) dir = g_dir_open(PNV_RUN_DIR, 0, NULL);
    const char *name;

    while (dir && (name = g_dir_read_name(dir))) {
        g_autofree char *path = g_build_filename(PNV_RUN_DIR, name, "subnet", NULL);
        PnvCidr4 c;
        if (pnv_uuid_valid(name) && !g_str_equal(name, uuid) && read_reservation(path, &c))
            g_array_append_val(used, c);
    }
}

gboolean pnv_pool_reserve(const char *uuid, const char *run_dir, guint32 *network, GError **error)
{
    g_autofree char *lock_path = g_build_filename(PNV_RUN_DIR, "pool.lock", NULL);
    g_autofree char *path = g_build_filename(run_dir, "subnet", NULL);
    g_autoptr(GArray) used = g_array_new(FALSE, FALSE, sizeof(PnvCidr4));
    PnvCidr4 p = pool();
    guint32 count = 1u << (30 - p.prefix), start = g_str_hash(uuid) % count;
    int lock = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    gboolean found = FALSE;

    if (lock < 0 || flock(lock, LOCK_EX) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "cannot lock %s: %s", lock_path,
                    g_strerror(errno));
        if (lock >= 0)
            close(lock);
        return FALSE;
    }
    dump(used, RTM_GETADDR);
    dump(used, RTM_GETROUTE);
    reserved(used, uuid);
    for (guint32 i = 0; i < count && !found; i++) {
        guint32 candidate = (p.address & (p.prefix ? ~0u << (32 - p.prefix) : 0)) + ((start + i) % count) * 4;
        found = TRUE;
        for (guint j = 0; j < used->len && found; j++) {
            PnvCidr4 *u = &g_array_index(used, PnvCidr4, j);
            found = !overlaps(candidate, 30, u->address, u->prefix);
        }
        if (found)
            *network = candidate;
    }
    if (found) {
        g_autofree char *text = g_strdup_printf("%u.%u.%u.%u/30\n", *network >> 24, *network >> 16 & 0xff,
                                                *network >> 8 & 0xff, *network & 0xff);
        found = g_file_set_contents_full(path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "no free /30 left in the TUN pool");
    }
    close(lock);
    return found;
}

/* The interface of the best IPv4 default route in the main table, not one of our tunnels: the parent the daemon
 * picks itself when NM leaves the VPN without one (5.5). */
char *pnv_default_route_device(void)
{
    struct {
        struct nlmsghdr nh;
        struct rtmsg rt;
    } req = {
        .nh = {.nlmsg_len = sizeof req, .nlmsg_type = RTM_GETROUTE, .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP},
        .rt = {.rtm_family = AF_INET},
    };
    char buf[32768], name[IF_NAMESIZE] = "";
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    guint32 best = G_MAXUINT32;
    gboolean done = FALSE;

    if (fd < 0 || send(fd, &req, sizeof req, 0) < 0)
        goto out;
    while (!done) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0)
            break;
        for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, (size_t)n); nh = NLMSG_NEXT(nh, n)) {
            struct rtmsg *rt = NLMSG_DATA(nh);
            int len = RTM_PAYLOAD(nh), oif = 0;
            guint32 metric = 0;
            char ifname[IF_NAMESIZE];

            if (nh->nlmsg_type == NLMSG_DONE || nh->nlmsg_type == NLMSG_ERROR) {
                done = TRUE;
                break;
            }
            if (rt->rtm_dst_len != 0 || rt->rtm_table != RT_TABLE_MAIN || rt->rtm_type != RTN_UNICAST)
                continue;
            for (struct rtattr *a = RTM_RTA(rt); RTA_OK(a, len); a = RTA_NEXT(a, len)) {
                if (a->rta_type == RTA_OIF)
                    oif = *(int *)RTA_DATA(a);
                else if (a->rta_type == RTA_PRIORITY)
                    metric = *(guint32 *)RTA_DATA(a);
            }
            if (oif && if_indextoname(oif, ifname) && !g_str_has_prefix(ifname, "pnv-") && metric < best) {
                best = metric;
                g_strlcpy(name, ifname, sizeof name);
            }
        }
    }
out:
    if (fd >= 0)
        close(fd);
    return *name ? g_strdup(name) : NULL;
}
