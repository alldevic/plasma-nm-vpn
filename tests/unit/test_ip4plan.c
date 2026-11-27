/* SPDX-License-Identifier: MIT */
/* The Ip4Config mapping (3.2): routes, the default-route flag, NEVER_DEFAULT and DNS. */
#include "pnv-daemon.h"

static GArray *cidrs(const char *text)
{
    GArray *list = NULL;
    if (text)
        g_assert_true(pnv_parse_cidr4_list("allowed-ips", text, 64, &list, NULL));
    return list;
}

static GPtrArray *servers(const char *text)
{
    GPtrArray *list = NULL;
    if (text)
        g_assert_true(pnv_parse_ip4_list("dns", text, 8, &list, NULL));
    return list;
}

static char *describe(const PnvIp4Plan *plan)
{
    GString *out = g_string_new(NULL);

    g_string_append_printf(out, "%s%s routes=", plan->default_route ? "default" : "narrow",
                           plan->never_default ? " never-default" : "");
    for (guint i = 0; i < plan->routes->len; i++) {
        PnvCidr4 c = g_array_index(plan->routes, PnvCidr4, i);
        g_string_append_printf(out, "%s%u.%u.%u.%u/%u", i ? "," : "", c.address >> 24, c.address >> 16 & 0xff,
                               c.address >> 8 & 0xff, c.address & 0xff, c.prefix);
    }
    g_string_append(out, " dns=");
    for (guint i = 0; i < plan->dns->len; i++)
        g_string_append_printf(out, "%s%s", i ? "," : "", (char *)plan->dns->pdata[i]);
    return g_string_free(out, FALSE);
}

static void test_plan(void)
{
    static const struct {
        const char *routes; /* NULL: a proxy core */
        gboolean never_default;
        const char *dns, *want;
    } cases[] = {
        {NULL, FALSE, NULL, "default routes= dns=1.1.1.1,9.9.9.9"},
        {NULL, TRUE, NULL, "narrow routes= dns="},
        {NULL, TRUE, "203.0.113.1", "narrow routes=203.0.113.1/32 dns=203.0.113.1"},
        {NULL, FALSE, "203.0.113.1", "default routes= dns=203.0.113.1"},
        {"0.0.0.0/0", FALSE, NULL, "default routes= dns=1.1.1.1,9.9.9.9"},
        {"0.0.0.0/1, 128.0.0.0/1", FALSE, NULL, "default routes= dns=1.1.1.1,9.9.9.9"},
        {"0.0.0.0/1, 128.0.0.0/1", TRUE, NULL, "narrow never-default routes= dns="},
        {"0.0.0.0/0", TRUE, "203.0.113.1", "narrow never-default routes=203.0.113.1/32 dns=203.0.113.1"},
        {"100.64.0.0/24, 203.0.113.0/24", FALSE, NULL, "narrow never-default routes=100.64.0.0/24,203.0.113.0/24 dns="},
        {"100.64.0.0/24", FALSE, "203.0.113.1",
         "narrow never-default routes=100.64.0.0/24,203.0.113.1/32 dns=203.0.113.1"},
        {"0.0.0.0/0, 10.0.0.0/8", FALSE, NULL, "default routes=10.0.0.0/8 dns=1.1.1.1,9.9.9.9"},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_autoptr(GArray) routes = cidrs(cases[i].routes);
        g_autoptr(GPtrArray) dns = servers(cases[i].dns);
        g_autoptr(PnvIp4Plan) plan = pnv_ip4_plan(routes, cases[i].never_default, dns);
        g_autofree char *got = describe(plan);

        if (g_strcmp0(got, cases[i].want) != 0)
            g_error("case %zu: got \"%s\", want \"%s\"", i, got, cases[i].want);
    }
}

static void test_ipv6_entries(void)
{
    g_autoptr(GArray) list = NULL;
    guint skipped = 0;

    g_assert_true(pnv_parse_cidr_list_v4("allowed-ips", "10.0.0.0/8, ::/0, 2001:db8::/32", 64, &list, &skipped, NULL));
    g_assert_cmpuint(list->len, ==, 1);
    g_assert_cmpuint(skipped, ==, 2);
    g_assert_false(pnv_parse_cidr_list_v4("allowed-ips", "2001:db8::/129", 64, &list, &skipped, NULL));
    g_assert_false(pnv_parse_cidr4_list("allowed-ips", "::/0", 64, &list, NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ip4plan/plan", test_plan);
    g_test_add_func("/ip4plan/ipv6-entries", test_ipv6_entries);
    return g_test_run();
}
