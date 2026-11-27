/* SPDX-License-Identifier: MIT */
/* What goes into Ip4Config (design.md, "Routing, DNS and IPv6", "DNS", "AmneziaWG rules in the MVP"), as a pure
 * function so that the mapping is unit-tested without NM. */
#include "pnv-daemon.h"

static const char *const default_dns[] = {"1.1.1.1", "9.9.9.9", NULL};

static gboolean is_default(PnvCidr4 c)
{
    return c.prefix == 0;
}

/* routes: the tunnel's own routes (AmneziaWG allowed-ips), NULL for the proxy cores, which carry everything. */
PnvIp4Plan *pnv_ip4_plan(const GArray *routes, gboolean never_default_setting, const GPtrArray *dns)
{
    PnvIp4Plan *plan = g_new0(PnvIp4Plan, 1);
    gboolean all = routes == NULL;

    plan->routes = g_array_new(FALSE, FALSE, sizeof(PnvCidr4));
    plan->dns = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; routes && i < routes->len; i++) {
        PnvCidr4 c = g_array_index(routes, PnvCidr4, i);
        if (is_default(c))
            all = TRUE; /* NM makes the default route itself; never passed as a route */
        else
            g_array_append_val(plan->routes, c);
    }
    plan->default_route = all && !never_default_setting;
    /* NM would otherwise put a default route through any VPN. */
    plan->never_default = routes != NULL && !plan->default_route;
    if (dns && dns->len) {
        for (guint i = 0; i < dns->len; i++) {
            g_ptr_array_add(plan->dns, g_strdup(dns->pdata[i]));
            if (!plan->default_route) {
                /* So that queries to an explicit server stay in the tunnel. */
                g_autoptr(GArray) host = NULL;
                if (pnv_parse_cidr4_list("dns", dns->pdata[i], 1, &host, NULL)) {
                    PnvCidr4 c = g_array_index(host, PnvCidr4, 0);
                    g_array_append_val(plan->routes, c);
                }
            }
        }
    } else if (plan->default_route) {
        for (const char *const *d = default_dns; *d; d++)
            g_ptr_array_add(plan->dns, g_strdup(*d));
    }
    return plan;
}

void pnv_ip4_plan_free(PnvIp4Plan *plan)
{
    if (!plan)
        return;
    g_array_unref(plan->routes);
    g_ptr_array_unref(plan->dns);
    g_free(plan);
}
