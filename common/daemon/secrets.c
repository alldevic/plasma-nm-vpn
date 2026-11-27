/* SPDX-License-Identifier: MIT */
#include "pnv-daemon.h"

gboolean pnv_secret_needed(NMSettingVpn *s_vpn, const PnvSecret *secret)
{
    NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;
    const char *when = secret->when ? nm_setting_vpn_get_data_item(s_vpn, secret->when) : NULL;

    if (secret->when && !(when && *when))
        return FALSE;
    if (secret->optional) {
        g_autofree char *flags_key = g_strconcat(secret->key, "-flags", NULL);
        if (!nm_setting_vpn_get_data_item(s_vpn, flags_key))
            return FALSE;
    }
    if (nm_setting_vpn_get_secret(s_vpn, secret->key) && *nm_setting_vpn_get_secret(s_vpn, secret->key))
        return FALSE;
    nm_setting_get_secret_flags(NM_SETTING(s_vpn), secret->key, &flags, NULL);
    return !(flags & NM_SETTING_SECRET_FLAG_NOT_REQUIRED);
}
