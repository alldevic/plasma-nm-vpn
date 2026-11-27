/* SPDX-License-Identifier: MIT */
/* plasma-nm-vpn-tun-fd NAME VAR COMMAND...: attach to the existing persistent TUN NAME as its owner, put it on
 * fd 3, set VAR=3 and exec COMMAND. xray given its TUN this way leaves the link alone and needs no capabilities
 * (0.4, 0.6). Runs as the core user in the core unit. */
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct ifreq ifr = {.ifr_flags = IFF_TUN | IFF_NO_PI};
    int fd;

    if (argc < 4 || strlen(argv[1]) >= IFNAMSIZ) {
        fprintf(stderr, "usage: plasma-nm-vpn-tun-fd NAME VAR COMMAND...\n");
        return 2;
    }
    strcpy(ifr.ifr_name, argv[1]);
    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0 || ioctl(fd, TUNSETIFF, &ifr) != 0) {
        fprintf(stderr, "cannot attach to %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    if (fd != 3 && (dup2(fd, 3) != 3 || close(fd) != 0)) {
        fprintf(stderr, "cannot move the TUN to fd 3: %s\n", strerror(errno));
        return 1;
    }
    if (setenv(argv[2], "3", 1) != 0)
        return 1;
    execv(argv[3], argv + 3);
    fprintf(stderr, "cannot run %s: %s\n", argv[3], strerror(errno));
    return 1;
}
