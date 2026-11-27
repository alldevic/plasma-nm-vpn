/* SPDX-License-Identifier: MIT */
#include "config.h"

static void cidrs(GString *out, const GArray *list)
{
    for (guint i = 0; i < list->len; i++) {
        PnvCidr4 c = g_array_index(list, PnvCidr4, i);
        g_string_append_printf(out, "%s%u.%u.%u.%u/%u", i ? ", " : "", c.address >> 24, c.address >> 16 & 0xff,
                               c.address >> 8 & 0xff, c.address & 0xff, c.prefix);
    }
}

char *pnv_awg_config(const PnvProfile *profile, const char *server_ip)
{
    const PnvAmneziawg *a = &profile->amneziawg;
    GString *out = g_string_new("[Interface]\n");

    g_string_append_printf(out, "PrivateKey = %s\n", a->private_key);
    for (guint i = 0; a->params && i < a->params->len; i++) {
        const PnvAwgParam *param = a->params->pdata[i];
        g_string_append_printf(out, "%s = %s\n", param->key, param->value);
    }
    if (a->header_protection_key)
        g_string_append_printf(out, "HeaderProtectionKey = %s\n", a->header_protection_key);
    g_string_append_printf(out, "\n[Peer]\nPublicKey = %s\n", a->peer_public_key);
    if (a->preshared_key)
        g_string_append_printf(out, "PresharedKey = %s\n", a->preshared_key);
    g_string_append_printf(out, strchr(server_ip, ':') ? "Endpoint = [%s]:%u\n" : "Endpoint = %s:%u\n", server_ip,
                           a->port);
    g_string_append(out, "AllowedIPs = ");
    cidrs(out, a->allowed_ips);
    g_string_append_c(out, '\n');
    if (a->keepalive)
        g_string_append_printf(out, "PersistentKeepalive = %u\n", a->keepalive);
    return g_string_free(out, FALSE);
}
