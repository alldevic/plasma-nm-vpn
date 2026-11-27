/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "pnv-daemon.h"

int main(void)
{
    return strcmp(pnv_version(), "") == 0;
}
