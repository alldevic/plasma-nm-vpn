/* SPDX-License-Identifier: MIT */
#include "pnv-daemon.h"

static const PnvSecret hysteria2[] = {{"password", "Password"}, {NULL}};

static const struct {
    const char *service;
    const PnvSecret *secrets;
} table[] = {
    {"org.freedesktop.NetworkManager.pnv.hysteria2", hysteria2},
};

const PnvSecret *pnv_service_secrets(const char *service)
{
    for (gsize i = 0; i < G_N_ELEMENTS(table); i++)
        if (g_strcmp0(table[i].service, service) == 0)
            return table[i].secrets;
    return NULL;
}

gboolean pnv_secret_missing(NMSettingVpn *s_vpn, const char *key)
{
    NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;

    if (nm_setting_vpn_get_secret(s_vpn, key))
        return FALSE;
    nm_setting_get_secret_flags(NM_SETTING(s_vpn), key, &flags, NULL);
    return !(flags & NM_SETTING_SECRET_FLAG_NOT_REQUIRED);
}
