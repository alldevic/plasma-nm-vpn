/* SPDX-License-Identifier: MIT */
#include <NetworkManager.h>
#include <stdio.h>
#include <string.h>

#include "pnv-daemon.h"

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--version") == 0)
        printf("%s (libnm %d.%d.%d)\n", pnv_version(), NM_MAJOR_VERSION, NM_MINOR_VERSION, NM_MICRO_VERSION);
    return 0;
}
