/* SPDX-License-Identifier: MIT */
#ifndef PNV_XRAY_CONFIG_H
#define PNV_XRAY_CONFIG_H

#include "pnv-profile.h"

typedef struct {
    const char *tun;
    const char *server_ip;
    const char *parent; /* sockopt.interface, always set: xray's autodetection is not used */
} PnvXrayTarget;

JsonNode *pnv_xray_load_template(const char *path, GError **error);
char *pnv_xray_vless_config(JsonNode *template, const PnvProfile *profile, const PnvXrayTarget *target,
                            GError **error);

#endif
