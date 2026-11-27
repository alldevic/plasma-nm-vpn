/* SPDX-License-Identifier: MIT */
#ifndef PNV_SB_CONFIG_H
#define PNV_SB_CONFIG_H

#include "pnv-profile.h"

/* What the session contributes to the config. */
typedef struct {
    const char *tun;
    const char *tun_cidr;
    guint mtu;
    const char *server_ip;
    const char *parent; /* NULL: no bind_interface */
} PnvSbTarget;

JsonNode *pnv_sb_load_template(const char *path, GError **error);
char *pnv_sb_hysteria2_config(JsonNode *template, const PnvProfile *profile, const PnvSbTarget *target,
                              GError **error);

#endif
