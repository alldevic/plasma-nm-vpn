/* SPDX-License-Identifier: MIT */
/* The persistent TUN pnv-<8 characters of the UUID>: owned by the daemon, attached to by the core (0.4). */
#include <fcntl.h>
#include <linux/if_tun.h>
#include <linux/rtnetlink.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "pnv-daemon.h"

void pnv_tun_name(const char *uuid, char name[IFNAMSIZ])
{
    g_snprintf(name, IFNAMSIZ, "pnv-%.8s", uuid);
}

gboolean pnv_tun_exists(const char *name)
{
    return if_nametoindex(name) != 0;
}

gboolean pnv_tun_create(const char *name, guint mtu, GError **error)
{
    struct ifreq ifr = {.ifr_flags = IFF_TUN | IFF_NO_PI};
    struct passwd *pw = getpwnam(PNV_CORE_USER);
    int fd, s = -1;
    gboolean ok;

    if (!pw) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no user %s", PNV_CORE_USER);
        return FALSE;
    }
    fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    g_strlcpy(ifr.ifr_name, name, IFNAMSIZ);
    ok = fd >= 0 && ioctl(fd, TUNSETIFF, &ifr) == 0 && ioctl(fd, TUNSETOWNER, pw->pw_uid) == 0 &&
         ioctl(fd, TUNSETPERSIST, 1) == 0;
    if (ok && mtu) {
        s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        ifr.ifr_mtu = mtu;
        ok = s >= 0 && ioctl(s, SIOCSIFMTU, &ifr) == 0;
    }
    if (!ok)
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "cannot create %s: %s", name, g_strerror(errno));
    if (s >= 0)
        close(s);
    if (fd >= 0)
        close(fd);
    if (!ok)
        pnv_tun_delete(name);
    return ok;
}

void pnv_tun_delete(const char *name)
{
    struct ifreq ifr = {.ifr_flags = IFF_TUN | IFF_NO_PI};
    int fd;

    if (!pnv_tun_exists(name) || (fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC)) < 0)
        return;
    g_strlcpy(ifr.ifr_name, name, IFNAMSIZ);
    if (ioctl(fd, TUNSETIFF, &ifr) == 0)
        ioctl(fd, TUNSETPERSIST, 0);
    close(fd);
}

/* IFLA_CARRIER via RTM_GETLINK: 1 once the core has the TUN open, whether the link is up or not; -1 if the
 * interface is gone. Polled: no RTM_NEWLINK arrives for it while the interface is down (0.4). */
int pnv_tun_carrier(const char *name)
{
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
    } req = {
        .nh = {.nlmsg_len = sizeof req, .nlmsg_type = RTM_GETLINK, .nlmsg_flags = NLM_F_REQUEST},
        .ifi = {.ifi_family = AF_UNSPEC, .ifi_index = (int)if_nametoindex(name)},
    };
    char buf[8192];
    int fd, carrier = -1;
    ssize_t n;

    if (!req.ifi.ifi_index || (fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE)) < 0)
        return -1;
    if (send(fd, &req, sizeof req, 0) > 0 && (n = recv(fd, buf, sizeof buf, 0)) > 0) {
        struct nlmsghdr *nh = (struct nlmsghdr *)buf;
        if (NLMSG_OK(nh, (size_t)n) && nh->nlmsg_type == RTM_NEWLINK) {
            int len = IFLA_PAYLOAD(nh);
            for (struct rtattr *a = IFLA_RTA(NLMSG_DATA(nh)); RTA_OK(a, len); a = RTA_NEXT(a, len))
                if (a->rta_type == IFLA_CARRIER)
                    carrier = *(unsigned char *)RTA_DATA(a);
        }
    }
    close(fd);
    return carrier;
}
