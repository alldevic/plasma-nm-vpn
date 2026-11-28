/* SPDX-License-Identifier: MIT */
/* Import (design.md, "plasma-nm user interface"): hysteria2:// and hy2:// per the official URI scheme, vless:// per
 * the XTLS standard (Xray-core #716), AmneziaWG .conf in the awg-quick format. The text is untrusted: every value
 * ends up checked by pnv_profile_parse(), and problems carry keys and positions, never values. */
#include <arpa/inet.h>
#include <string.h>

#include "pnv-profile.h"

static gboolean fail(PnvProblem *p, PnvCode code, const char *key, const char *detail, gint64 position, gint64 max)
{
    if (p) {
        pnv_problem_clear(p);
        *p = (PnvProblem){code, g_strdup(key), g_strdup(detail), position, 0, max};
    }
    return FALSE;
}

static void warn(PnvImport *import, PnvCode code, const char *key, gint64 position)
{
    PnvProblem w = {code, g_strdup(key), NULL, position, 0, 0};
    g_array_append_val(import->warnings, w);
}

static void clear_warning(gpointer w)
{
    pnv_problem_clear(w);
}

static PnvImport *import_new(const char *service)
{
    PnvImport *import = g_new0(PnvImport, 1);
    import->service = service;
    import->data = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    import->secrets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    import->warnings = g_array_new(FALSE, TRUE, sizeof(PnvProblem));
    g_array_set_clear_func(import->warnings, clear_warning);
    return import;
}

void pnv_import_free(PnvImport *import)
{
    if (!import)
        return;
    g_hash_table_unref(import->data);
    g_hash_table_unref(import->secrets);
    g_array_unref(import->warnings);
    g_free(import);
}

static void put(GHashTable *table, const char *key, const char *value)
{
    if (value && *value)
        g_hash_table_replace(table, g_strdup(key), g_strdup(value));
}

static gboolean has_prefix_ci(const char *text, const char *prefix)
{
    return g_ascii_strncasecmp(text, prefix, strlen(prefix)) == 0;
}

static const char *skip_space(const char *text)
{
    while (g_ascii_isspace(*text))
        text++;
    return text;
}

const char *pnv_import_service(const char *text)
{
    text = skip_space(text);
    if (has_prefix_ci(text, "hysteria2://") || has_prefix_ci(text, "hy2://") || has_prefix_ci(text, "hysteria2+"))
        return PNV_SERVICE_HYSTERIA2;
    if (has_prefix_ci(text, "vless://"))
        return PNV_SERVICE_VLESS;
    for (const char *line = text; line; line = strchr(line, '\n')) {
        line = skip_space(line);
        if (has_prefix_ci(line, "[interface]"))
            return PNV_SERVICE_AMNEZIAWG;
    }
    return NULL;
}

/* ---- Links ---- */

typedef struct {
    char *userinfo;    /* decoded */
    char *host;        /* without brackets */
    char *port;        /* as written: one port or a list of ports and ranges */
    GHashTable *query; /* decoded name → decoded value, in a separate list for order */
    GPtrArray *order;
} Link;

static void link_clear(Link *l)
{
    g_free(l->userinfo);
    g_free(l->host);
    g_free(l->port);
    g_clear_pointer(&l->query, g_hash_table_unref);
    g_clear_pointer(&l->order, g_ptr_array_unref);
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(Link, link_clear)

/* Percent-decoding that refuses an encoded NUL. */
static char *decode(const char *text, gsize length)
{
    g_autofree char *raw = g_strndup(text, length);
    return g_uri_unescape_segment(raw, NULL, NULL);
}

/* scheme://[userinfo@]host[:port][/][?query][#fragment]; the scheme is already known and skipped. */
static gboolean parse_link(const char *rest, Link *l, PnvProblem *p)
{
    const char *end = rest + strcspn(rest, "#");
    const char *query = memchr(rest, '?', end - rest);
    const char *auth_end = query ? query : end;
    const char *slash = memchr(rest, '/', auth_end - rest);
    const char *at, *host, *host_end, *port = NULL;

    /* Only an empty path or "/" before the query. */
    if (slash) {
        if (slash + 1 != auth_end)
            return fail(p, PNV_E_IMPORT_BAD_LINK, NULL, "path", 0, 0);
        auth_end = slash;
    }
    at = g_strrstr_len(rest, auth_end - rest, "@");
    if (at) {
        l->userinfo = decode(rest, at - rest);
        if (!l->userinfo)
            return fail(p, PNV_E_IMPORT_BAD_LINK, NULL, "encoding", 0, 0);
    }
    host = at ? at + 1 : rest;
    if (*host == '[') {
        host_end = memchr(host, ']', auth_end - host);
        if (!host_end)
            return fail(p, PNV_E_IMPORT_BAD_LINK, NULL, "host", 0, 0);
        l->host = g_strndup(host + 1, host_end - host - 1);
        host_end++;
        if (host_end < auth_end) {
            if (*host_end != ':')
                return fail(p, PNV_E_IMPORT_BAD_LINK, NULL, "host", 0, 0);
            port = host_end + 1;
        }
    } else {
        const char *colon = memchr(host, ':', auth_end - host);
        host_end = colon ? colon : auth_end;
        l->host = g_strndup(host, host_end - host);
        if (colon)
            port = colon + 1;
    }
    if (!*l->host)
        return fail(p, PNV_E_IMPORT_BAD_LINK, NULL, "host", 0, 0);
    if (port) {
        l->port = g_strndup(port, auth_end - port);
        if (!*l->port)
            return fail(p, PNV_E_IMPORT_BAD_LINK, NULL, "port", 0, 0);
    }
    l->query = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    l->order = g_ptr_array_new();
    for (const char *item = query ? query + 1 : end; item < end;) {
        const char *item_end = item + strcspn(item, "&#");
        const char *eq = memchr(item, '=', item_end - item);
        g_autofree char *name = NULL, *value = NULL;

        if (item_end > end)
            item_end = end;
        if (item == item_end) {
            item++;
            continue;
        }
        name = decode(item, (eq ? eq : item_end) - item);
        value = eq ? decode(eq + 1, item_end - eq - 1) : g_strdup("");
        if (!name || !value)
            return fail(p, PNV_E_IMPORT_BAD_LINK, NULL, "encoding", 0, 0);
        if (g_hash_table_contains(l->query, name))
            return fail(p, PNV_E_IMPORT_DUPLICATE, name, NULL, 0, 0);
        g_ptr_array_add(l->order, name);
        g_hash_table_insert(l->query, g_steal_pointer(&name), g_steal_pointer(&value));
        item = item_end + 1;
    }
    return TRUE;
}

/* A parameter the form has no field for: into extra if the core's allowlist names it, otherwise dropped. */
static void unknown_param(PnvImport *import, const char *name, const char *value, const PnvList *lists,
                          JsonObject *extra)
{
    g_autofree char *entry = g_strconcat("extra/", name, NULL);

    if (lists && pnv_list_contains(lists, entry) && !strchr(name, '.')) {
        json_object_set_string_member(extra, name, value);
        warn(import, PNV_W_TO_EXTRA, name, 0);
        return;
    }
    warn(import, PNV_W_DROPPED, name, 0);
}

static void put_extra(PnvImport *import, JsonObject *extra)
{
    g_autoptr(JsonNode) node = NULL;
    g_autofree char *text = NULL;

    if (!json_object_get_size(extra))
        return;
    node = json_node_init_object(json_node_alloc(), extra);
    text = json_to_string(node, FALSE);
    put(import->data, "extra", text);
}

static gboolean link_bool(const char *value, const char **out)
{
    if (g_str_equal(value, "1") || g_ascii_strcasecmp(value, "true") == 0)
        *out = "yes";
    else if (g_str_equal(value, "0") || g_ascii_strcasecmp(value, "false") == 0)
        *out = "no";
    else
        return FALSE;
    return TRUE;
}

static PnvImport *import_hysteria2(const char *text, const PnvList *lists, PnvProblem *p)
{
    g_autoptr(PnvImport) import = import_new(PNV_SERVICE_HYSTERIA2);
    g_autoptr(JsonObject) extra = json_object_new();
    g_auto(Link) l = {0};
    const char *rest;

    text = skip_space(text);
    if (has_prefix_ci(text, "hysteria2+"))
        return fail(p, PNV_E_IMPORT_UNSUPPORTED, NULL, "hysteria2+realm", 0, 0), NULL;
    rest = strstr(text, "://") + 3;
    if (!parse_link(rest, &l, p))
        return NULL;
    put(import->data, "server", l.host);
    /* The scheme's default port. */
    put(import->data, "ports", l.port ? l.port : "443");
    if (l.userinfo)
        put(import->secrets, "password", l.userinfo);
    for (guint i = 0; i < l.order->len; i++) {
        const char *name = l.order->pdata[i], *value = g_hash_table_lookup(l.query, name), *b;

        if (g_str_equal(name, "obfs")) {
            if (g_ascii_strcasecmp(value, "gecko") == 0)
                return fail(p, PNV_E_IMPORT_UNSUPPORTED, NULL, "gecko", 0, 0), NULL;
            put(import->data, "obfs", value);
        } else if (g_str_equal(name, "obfs-password")) {
            put(import->secrets, "obfs-password", value);
        } else if (g_str_equal(name, "sni")) {
            put(import->data, "tls-sni", value);
        } else if (g_str_equal(name, "insecure")) {
            if (!link_bool(value, &b))
                return fail(p, PNV_E_BAD_BOOL, "insecure", NULL, 0, 0), NULL;
            put(import->data, "tls-insecure", b);
        } else if (g_str_equal(name, "pinSHA256")) {
            /* sing-box has no equivalent (2.5): a dropped pin would weaken the TLS check. */
            return fail(p, PNV_E_IMPORT_UNSUPPORTED, NULL, "pinSHA256", 0, 0), NULL;
        } else if (g_str_equal(name, "ech")) {
            return fail(p, PNV_E_IMPORT_UNSUPPORTED, NULL, "ech", 0, 0), NULL;
        } else {
            unknown_param(import, name, value, lists, extra);
        }
    }
    put_extra(import, extra);
    return g_steal_pointer(&import);
}

static PnvImport *import_vless(const char *text, const PnvList *lists, PnvProblem *p)
{
    static const struct {
        const char *param, *key;
    } fields[] = {{"encryption", "encryption"}, {"pbk", "reality-pbk"}, {"sid", "reality-sid"},
                  {"spx", "reality-spx"},       {"fp", "tls-fingerprint"}, {"sni", "tls-sni"},
                  {"path", "xhttp-path"},       {"host", "xhttp-host"},   {"mode", "xhttp-mode"},
                  {"extra", "xhttp-extra"}};
    g_autoptr(PnvImport) import = import_new(PNV_SERVICE_VLESS);
    g_autoptr(JsonObject) extra = json_object_new();
    g_auto(Link) l = {0};
    const char *type, *security;

    if (!parse_link(strstr(skip_space(text), "://") + 3, &l, p))
        return NULL;
    type = g_hash_table_lookup(l.query, "type");
    security = g_hash_table_lookup(l.query, "security");
    /* The standard's defaults are tcp and none. */
    if (!type || !g_str_equal(type, "xhttp"))
        return fail(p, PNV_E_IMPORT_NOT_REALITY, "type", NULL, 0, 0), NULL;
    if (!security || !g_str_equal(security, "reality"))
        return fail(p, PNV_E_IMPORT_NOT_REALITY, "security", NULL, 0, 0), NULL;
    if (!l.port)
        return fail(p, PNV_E_IMPORT_BAD_LINK, NULL, "port", 0, 0), NULL;
    put(import->data, "server", l.host);
    put(import->data, "port", l.port);
    if (l.userinfo)
        put(import->secrets, "uuid", l.userinfo);
    for (guint i = 0; i < l.order->len; i++) {
        const char *name = l.order->pdata[i], *value = g_hash_table_lookup(l.query, name);
        gboolean known = g_str_equal(name, "type") || g_str_equal(name, "security");

        for (guint f = 0; !known && f < G_N_ELEMENTS(fields); f++)
            if (g_str_equal(name, fields[f].param)) {
                put(import->data, fields[f].key, value);
                known = TRUE;
            }
        if (known)
            continue;
        /* pcs, vcn and allowInsecure are not needed for Reality (and allowInsecure is gone from Xray-core). */
        if (g_str_equal(name, "pcs") || g_str_equal(name, "vcn") || g_str_equal(name, "allowInsecure"))
            warn(import, PNV_W_DROPPED, name, 0);
        else
            unknown_param(import, name, value, lists, extra);
    }
    put_extra(import, extra);
    return g_steal_pointer(&import);
}

/* ---- AmneziaWG .conf ---- */

typedef struct {
    GString *address, *dns, *allowed_ips; /* entries of repeated keys, joined */
} Lists;

static void lists_clear(Lists *l)
{
    if (l->address)
        g_string_free(l->address, TRUE);
    if (l->dns)
        g_string_free(l->dns, TRUE);
    if (l->allowed_ips)
        g_string_free(l->allowed_ips, TRUE);
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(Lists, lists_clear)

static void append(GString **list, const char *value)
{
    if (!*list)
        *list = g_string_new(value);
    else
        g_string_append_printf(*list, ",%s", value);
}

static gboolean is_ip(int family, const char *text)
{
    guint8 buf[16];
    g_autofree char *bare = g_strdup(text);
    char *slash = strchr(bare, '/');

    if (slash)
        *slash = '\0';
    return inet_pton(family, bare, buf) == 1;
}

/* Address: the first IPv4 entry, the others dropped with a warning; an entry without a prefix is a host. */
static void take_address(PnvImport *import, const char *value)
{
    g_auto(GStrv) items = g_strsplit(value, ",", -1);
    gboolean taken = FALSE;

    for (guint i = 0; items[i]; i++) {
        const char *item = g_strstrip(items[i]);
        if (is_ip(AF_INET6, item)) {
            warn(import, PNV_W_IPV6, "Address", i);
        } else if (taken) {
            warn(import, PNV_W_EXTRA_ADDRESS, "Address", i);
        } else {
            g_autofree char *address = strchr(item, '/') ? g_strdup(item) : g_strconcat(item, "/32", NULL);
            put(import->data, "address", address);
            taken = TRUE;
        }
    }
}

/* DNS: IPv4 entries; IPv6 and search domains (wg-quick takes both there) dropped with a warning each. */
static void take_dns(PnvImport *import, const char *value)
{
    g_auto(GStrv) items = g_strsplit(value, ",", -1);
    g_autoptr(GPtrArray) kept = g_ptr_array_new_null_terminated(0, NULL, TRUE);
    g_autofree char *joined = NULL;

    for (guint i = 0; items[i]; i++) {
        char *item = g_strstrip(items[i]);
        if (!*item)
            continue;
        if (is_ip(AF_INET, item) && !strchr(item, '/'))
            g_ptr_array_add(kept, item);
        else if (is_ip(AF_INET6, item))
            warn(import, PNV_W_IPV6, "DNS", i);
        else
            warn(import, PNV_W_DNS_NAME, "DNS", i);
    }
    joined = g_strjoinv(", ", (char **)kept->pdata);
    put(import->data, "dns", joined);
}

/* AllowedIPs: IPv6 entries dropped with a warning each; the IPv4 ones normalized by the 2.2 library (so
 * 0.0.0.0/1 + 128.0.0.0/1 becomes 0.0.0.0/0, as in the editor and the daemon). */
static gboolean take_allowed_ips(PnvImport *import, const char *value, PnvProblem *p)
{
    g_auto(GStrv) items = g_strsplit(value, ",", -1);
    g_autoptr(GPtrArray) v4 = g_ptr_array_new_null_terminated(0, NULL, TRUE);
    g_autofree char *joined = NULL;
    g_autoptr(GString) out = g_string_new(NULL);
    GArray *list = NULL;

    for (guint i = 0; items[i]; i++) {
        char *item = g_strstrip(items[i]);
        if (is_ip(AF_INET6, item))
            warn(import, PNV_W_IPV6, "AllowedIPs", i);
        else
            g_ptr_array_add(v4, item);
    }
    if (!v4->len)
        return TRUE;
    joined = g_strjoinv(",", (char **)v4->pdata);
    if (!pnv_parse_cidr4_list("allowed-ips", joined, 64, &list, p))
        return FALSE;
    for (guint i = 0; i < list->len; i++) {
        PnvCidr4 c = g_array_index(list, PnvCidr4, i);
        g_string_append_printf(out, "%s%u.%u.%u.%u/%u", i ? ", " : "", c.address >> 24, (c.address >> 16) & 0xff,
                               (c.address >> 8) & 0xff, c.address & 0xff, c.prefix);
    }
    g_array_unref(list);
    put(import->data, "allowed-ips", out->str);
    return TRUE;
}

static gboolean take_endpoint(PnvImport *import, const char *value, PnvProblem *p)
{
    const char *colon = strrchr(value, ':');
    g_autofree char *host = NULL;

    if (!colon || colon == value || !colon[1])
        return fail(p, PNV_E_IMPORT_BAD_LINK, "Endpoint", "port", 0, 0);
    host = value[0] == '[' && colon[-1] == ']' ? g_strndup(value + 1, colon - value - 2) : g_strndup(value, colon - value);
    put(import->data, "server", host);
    put(import->data, "port", colon + 1);
    return TRUE;
}

/* The awg-<parameter> whose awg config key is name, from the allowlist; NULL if there is none. */
static char *awg_key(const PnvList *lists, const char *name)
{
    for (guint i = 0; lists && i < lists->entries->len; i++) {
        const char *entry = lists->entries->pdata[i];
        const char *const *fields;

        if (!g_str_has_prefix(entry, "awg/"))
            continue;
        fields = pnv_list_fields(lists, entry);
        if (fields && fields[0] && g_ascii_strcasecmp(fields[0], name) == 0)
            return g_strdup(entry + 4);
    }
    return NULL;
}

static PnvImport *import_amneziawg(const char *text, const PnvList *lists, PnvProblem *p)
{
    static const char *const wg_quick[] = {"PreUp", "PostUp", "PreDown", "PostDown", "SaveConfig", "Table"};
    g_autoptr(PnvImport) import = import_new(PNV_SERVICE_AMNEZIAWG);
    g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
    g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_auto(Lists) repeated = {0};
    enum { NONE, INTERFACE, PEER } section = NONE;
    guint peers = 0;

    for (guint n = 0; lines[n]; n++) {
        char *line = lines[n], *eq;
        g_autofree char *key = NULL, *seen_key = NULL;
        const char *value;

        /* wg-quick comments run to the end of the line. */
        line[strcspn(line, "#")] = '\0';
        g_strstrip(line);
        if (!*line)
            continue;
        if (line[0] == '[') {
            if (g_ascii_strcasecmp(line, "[Interface]") == 0) {
                section = INTERFACE;
            } else if (g_ascii_strcasecmp(line, "[Peer]") == 0) {
                section = PEER;
                if (++peers > 1)
                    return fail(p, PNV_E_IMPORT_PEERS, NULL, NULL, 0, 0), NULL;
            } else {
                return fail(p, PNV_E_IMPORT_BAD_LINE, NULL, NULL, n + 1, 0), NULL;
            }
            continue;
        }
        eq = strchr(line, '=');
        if (!eq || section == NONE)
            return fail(p, PNV_E_IMPORT_BAD_LINE, NULL, NULL, n + 1, 0), NULL;
        *eq = '\0';
        key = g_strdup(g_strstrip(line));
        value = g_strstrip(eq + 1);
        if (!*key || strspn(key, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != strlen(key))
            return fail(p, PNV_E_IMPORT_BAD_LINE, NULL, NULL, n + 1, 0), NULL;
        /* Address, DNS and AllowedIPs may repeat and add up, as in wg-quick; other keys may not. */
        if (g_ascii_strcasecmp(key, "Address") == 0 && section == INTERFACE) {
            append(&repeated.address, value);
            continue;
        }
        if (g_ascii_strcasecmp(key, "DNS") == 0 && section == INTERFACE) {
            append(&repeated.dns, value);
            continue;
        }
        if (g_ascii_strcasecmp(key, "AllowedIPs") == 0 && section == PEER) {
            append(&repeated.allowed_ips, value);
            continue;
        }
        seen_key = g_strdup_printf("%d/%s", section, key);
        for (char *c = seen_key; *c; c++)
            *c = g_ascii_tolower(*c);
        if (!g_hash_table_add(seen, g_steal_pointer(&seen_key)))
            return fail(p, PNV_E_IMPORT_DUPLICATE, key, NULL, 0, 0), NULL;
        gboolean hook = FALSE;
        for (guint i = 0; i < G_N_ELEMENTS(wg_quick); i++)
            hook = hook || g_ascii_strcasecmp(key, wg_quick[i]) == 0;
        if (hook) {
            warn(import, PNV_W_WG_QUICK, key, 0);
            continue;
        }
        if (section == INTERFACE) {
            g_autofree char *param = NULL;
            if (g_ascii_strcasecmp(key, "PrivateKey") == 0)
                put(import->secrets, "private-key", value);
            else if (g_ascii_strcasecmp(key, "HeaderProtectionKey") == 0)
                put(import->secrets, "awg-headerprotectionkey", value);
            else if (g_ascii_strcasecmp(key, "MTU") == 0)
                put(import->data, "mtu", value);
            else if ((param = awg_key(lists, key)))
                put(import->data, param, value);
            else
                warn(import, PNV_W_DROPPED, key, 0);
        } else {
            if (g_ascii_strcasecmp(key, "PublicKey") == 0)
                put(import->data, "peer-public-key", value);
            else if (g_ascii_strcasecmp(key, "PresharedKey") == 0)
                put(import->secrets, "preshared-key", value);
            else if (g_ascii_strcasecmp(key, "Endpoint") == 0) {
                if (!take_endpoint(import, value, p))
                    return NULL;
            } else if (g_ascii_strcasecmp(key, "PersistentKeepalive") == 0) {
                if (g_ascii_strcasecmp(value, "off") != 0)
                    put(import->data, "keepalive", value);
            } else {
                warn(import, PNV_W_DROPPED, key, 0);
            }
        }
    }
    if (repeated.address)
        take_address(import, repeated.address->str);
    if (repeated.dns)
        take_dns(import, repeated.dns->str);
    if (repeated.allowed_ips && !take_allowed_ips(import, repeated.allowed_ips->str, p))
        return NULL;
    return g_steal_pointer(&import);
}

PnvImport *pnv_import(const char *text, const PnvList *lists, PnvProblem *p)
{
    const char *service;
    g_autoptr(PnvImport) import = NULL;
    g_autoptr(GHashTable) secrets = NULL;
    g_autofree char *link = NULL;

    if (strlen(text) > PNV_IMPORT_MAX_SIZE)
        return fail(p, PNV_E_IMPORT_TOO_LARGE, NULL, NULL, 0, PNV_IMPORT_MAX_SIZE), NULL;
    if (!g_utf8_validate(text, -1, NULL))
        return fail(p, PNV_E_NOT_UTF8, NULL, NULL, 0, 0), NULL;
    service = pnv_import_service(text);
    if (!service)
        return fail(p, PNV_E_IMPORT_FORMAT, NULL, NULL, 0, 0), NULL;
    /* A pasted link often brings a line break or spaces around it. */
    link = g_strstrip(g_strdup(text));
    if (g_str_equal(service, PNV_SERVICE_HYSTERIA2))
        import = import_hysteria2(link, lists, p);
    else if (g_str_equal(service, PNV_SERVICE_VLESS))
        import = import_vless(link, lists, p);
    else
        import = import_amneziawg(text, lists, p);
    if (!import)
        return NULL;
    /* Everything through the same validators as the editors and the daemon; the secrets may be missing (an export
     * without them). */
    secrets = g_hash_table_ref(import->secrets);
    if (!pnv_profile_check(service, import->data, secrets, lists, p))
        return NULL;
    return g_steal_pointer(&import);
}
