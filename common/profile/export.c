/* SPDX-License-Identifier: MIT */
/* Export (design.md, "plasma-nm user interface"): the formats import reads, with what they cannot carry reported. */
#include <string.h>

#include "pnv-profile.h"

static const char *get(GHashTable *t, const char *key)
{
    const char *v = t ? g_hash_table_lookup(t, key) : NULL;
    return v && *v ? v : NULL;
}

static void warn(GArray *warnings, PnvCode code, const char *key)
{
    PnvProblem w = {code, g_strdup(key), NULL, 0, 0, 0};
    if (warnings)
        g_array_append_val(warnings, w);
}

/* A value the format has no place for; a field at its default (kill switch off, local network allowed) is not lost.
 * carried: the keys the format writes, NULL-terminated. */
static void report_lost(GHashTable *data, const char *const *carried, GArray *warnings)
{
    g_autoptr(GList) keys = g_list_sort(g_hash_table_get_keys(data), (GCompareFunc)strcmp);

    for (GList *k = keys; k; k = k->next) {
        const char *key = k->data, *value = get(data, key);
        if (!value || g_str_equal(key, "schema") || g_str_has_suffix(key, "-flags") || g_strv_contains(carried, key))
            continue;
        if ((g_str_equal(key, "kill-switch") && g_str_equal(value, "no")) ||
            (g_str_equal(key, "allow-lan") && g_str_equal(value, "yes")))
            continue;
        warn(warnings, PNV_W_EXPORT_LOST, key);
    }
}

/* A secret to write, or none: asked every time (NotSaved) is reported, not required and empty is left out. */
static const char *secret(GHashTable *data, GHashTable *secrets, const char *key, GArray *warnings)
{
    g_autofree char *flags_key = g_strconcat(key, "-flags", NULL);
    const char *flags = get(data, flags_key);

    if (!secrets)
        return NULL;
    if (get(secrets, key))
        return get(secrets, key);
    if (flags && (g_ascii_strtoull(flags, NULL, 10) & 2) && !(g_ascii_strtoull(flags, NULL, 10) & 4))
        warn(warnings, PNV_W_EXPORT_NO_SECRET, key);
    return NULL;
}

static char *escape(const char *value)
{
    return g_uri_escape_string(value, NULL, FALSE);
}

/* An IPv6 literal goes in brackets. */
static char *host(const char *server)
{
    return strchr(server, ':') ? g_strdup_printf("[%s]", server) : g_strdup(server);
}

static void param(GString *link, gboolean *first, const char *name, const char *value)
{
    g_autofree char *escaped = NULL;

    if (!value)
        return;
    escaped = escape(value);
    g_string_append_printf(link, "%c%s=%s", *first ? '?' : '&', name, escaped);
    *first = FALSE;
}

static char *export_hysteria2(GHashTable *data, GHashTable *secrets, GArray *warnings)
{
    static const char *const carried[] = {"server", "ports", "obfs", "tls-sni", "tls-insecure", NULL};
    const char *password = secret(data, secrets, "password", warnings);
    const char *obfs_password = secret(data, secrets, "obfs-password", warnings);
    g_autofree char *server = host(get(data, "server") ? get(data, "server") : "");
    GString *link = g_string_new("hysteria2://");
    gboolean first = TRUE;

    if (password) {
        g_autofree char *auth = escape(password);
        g_string_append_printf(link, "%s@", auth);
    }
    /* The ports are written as stored: one port or the link format's list. */
    g_string_append_printf(link, "%s:%s/", server, get(data, "ports") ? get(data, "ports") : "443");
    param(link, &first, "obfs", get(data, "obfs"));
    param(link, &first, "obfs-password", get(data, "obfs") ? obfs_password : NULL);
    param(link, &first, "sni", get(data, "tls-sni"));
    if (g_strcmp0(get(data, "tls-insecure"), "yes") == 0)
        param(link, &first, "insecure", "1");
    report_lost(data, carried, warnings);
    return g_string_free(link, FALSE);
}

static char *export_vless(GHashTable *data, GHashTable *secrets, GArray *warnings)
{
    static const struct {
        const char *param, *key;
    } fields[] = {{"encryption", "encryption"}, {"pbk", "reality-pbk"}, {"sid", "reality-sid"},
                  {"spx", "reality-spx"},       {"fp", "tls-fingerprint"}, {"sni", "tls-sni"},
                  {"path", "xhttp-path"},       {"host", "xhttp-host"},   {"mode", "xhttp-mode"},
                  {"extra", "xhttp-extra"}};
    static const char *const carried[] = {"server",      "port",          "encryption", "reality-pbk",
                                          "reality-sid", "reality-spx",   "tls-fingerprint", "tls-sni",
                                          "xhttp-path",  "xhttp-host",    "xhttp-mode", "xhttp-extra", NULL};
    const char *uuid = secret(data, secrets, "uuid", warnings);
    g_autofree char *server = host(get(data, "server") ? get(data, "server") : "");
    GString *link = g_string_new("vless://");
    gboolean first = TRUE;

    if (uuid) {
        g_autofree char *id = escape(uuid);
        g_string_append_printf(link, "%s@", id);
    }
    g_string_append_printf(link, "%s:%s", server, get(data, "port") ? get(data, "port") : "443");
    param(link, &first, "type", "xhttp");
    param(link, &first, "security", "reality");
    /* encryption as stored: none or the ML-KEM client string. */
    param(link, &first, "encryption", get(data, "encryption") ? get(data, "encryption") : "none");
    for (guint i = 1; i < G_N_ELEMENTS(fields); i++)
        param(link, &first, fields[i].param, get(data, fields[i].key));
    report_lost(data, carried, warnings);
    return g_string_free(link, FALSE);
}

/* The awg config key of an awg-<parameter>, from the allowlist. */
static const char *awg_name(const PnvList *lists, const char *key)
{
    g_autofree char *entry = g_strconcat("awg/", key, NULL);
    const char *const *fields = lists ? pnv_list_fields(lists, entry) : NULL;
    return fields && fields[0] ? fields[0] : NULL;
}

static char *export_amneziawg(GHashTable *data, GHashTable *secrets, const PnvList *lists, GArray *warnings)
{
    static const char *const carried[] = {"server",      "port",      "address", "dns",
                                          "mtu",         "peer-public-key", "allowed-ips", "keepalive", NULL};
    const char *private_key = secret(data, secrets, "private-key", warnings);
    const char *preshared_key = secret(data, secrets, "preshared-key", warnings);
    const char *header_key = secret(data, secrets, "awg-headerprotectionkey", warnings);
    g_autoptr(GList) keys = g_list_sort(g_hash_table_get_keys(data), (GCompareFunc)strcmp);
    g_autofree char *server = host(get(data, "server") ? get(data, "server") : "");
    g_autoptr(GPtrArray) params = g_ptr_array_new();
    GString *conf = g_string_new("[Interface]\n");

    if (private_key)
        g_string_append_printf(conf, "PrivateKey = %s\n", private_key);
    if (get(data, "address"))
        g_string_append_printf(conf, "Address = %s\n", get(data, "address"));
    /* No DNS line without dns: on import that leaves dns empty again. */
    if (get(data, "dns"))
        g_string_append_printf(conf, "DNS = %s\n", get(data, "dns"));
    if (get(data, "mtu"))
        g_string_append_printf(conf, "MTU = %s\n", get(data, "mtu"));
    for (GList *k = keys; k; k = k->next) {
        const char *key = k->data, *name = g_str_has_prefix(key, "awg-") ? awg_name(lists, key) : NULL;
        if (name && get(data, key)) {
            g_string_append_printf(conf, "%s = %s\n", name, get(data, key));
            g_ptr_array_add(params, k->data);
        }
    }
    if (header_key)
        g_string_append_printf(conf, "HeaderProtectionKey = %s\n", header_key);
    g_string_append(conf, "\n[Peer]\n");
    if (get(data, "peer-public-key"))
        g_string_append_printf(conf, "PublicKey = %s\n", get(data, "peer-public-key"));
    if (preshared_key)
        g_string_append_printf(conf, "PresharedKey = %s\n", preshared_key);
    g_string_append_printf(conf, "Endpoint = %s:%s\n", server, get(data, "port") ? get(data, "port") : "");
    if (get(data, "allowed-ips"))
        g_string_append_printf(conf, "AllowedIPs = %s\n", get(data, "allowed-ips"));
    if (get(data, "keepalive"))
        g_string_append_printf(conf, "PersistentKeepalive = %s\n", get(data, "keepalive"));
    {
        g_autoptr(GPtrArray) all = g_ptr_array_new();
        for (guint i = 0; carried[i]; i++)
            g_ptr_array_add(all, (gpointer)carried[i]);
        for (guint i = 0; i < params->len; i++)
            g_ptr_array_add(all, params->pdata[i]);
        g_ptr_array_add(all, NULL);
        report_lost(data, (const char *const *)all->pdata, warnings);
    }
    return g_string_free(conf, FALSE);
}

char *pnv_export(const char *service, GHashTable *data, GHashTable *secrets, const PnvList *lists, GArray *warnings)
{
    if (g_strcmp0(service, PNV_SERVICE_HYSTERIA2) == 0)
        return export_hysteria2(data, secrets, warnings);
    if (g_strcmp0(service, PNV_SERVICE_VLESS) == 0)
        return export_vless(data, secrets, warnings);
    if (g_strcmp0(service, PNV_SERVICE_AMNEZIAWG) == 0)
        return export_amneziawg(data, secrets, lists, warnings);
    return NULL;
}
