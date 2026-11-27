/* SPDX-License-Identifier: MIT */
/* The sing-box config for Hysteria2: the template for the pinned core version, filled from the validated
 * profile and the session. Only this generated config reaches the core. */
#include "config.h"

static JsonObject *object(JsonObject *parent, const char *member)
{
    return json_object_get_object_member(parent, member);
}

static void set_string(JsonObject *o, const char *member, const char *value)
{
    json_object_set_string_member(o, member, value);
}

static JsonArray *strings(const GPtrArray *values)
{
    JsonArray *a = json_array_new();
    for (guint i = 0; values && i < values->len; i++)
        json_array_add_string_element(a, values->pdata[i]);
    return a;
}

/* extra goes into the outbound only; its objects merge into the generator's (tls), its other members are added.
 * The validator has already checked every path against the allowlist. */
static void merge(JsonObject *into, JsonObject *from)
{
    JsonObjectIter iter;
    const char *name;
    JsonNode *node;

    json_object_iter_init(&iter, from);
    while (json_object_iter_next(&iter, &name, &node)) {
        if (JSON_NODE_HOLDS_OBJECT(node) && json_object_has_member(into, name) &&
            JSON_NODE_HOLDS_OBJECT(json_object_get_member(into, name)))
            merge(object(into, name), json_node_get_object(node));
        else
            json_object_set_member(into, name, json_node_copy(node));
    }
}

/* 443 → server_port; anything else → server_ports with a:b ranges (a single port is a:a) and hop_interval. */
static void set_ports(JsonObject *outbound, const PnvHysteria2 *h)
{
    JsonArray *ranges;
    g_autofree char *interval = NULL;

    if (h->ports->len == 1 && g_array_index(h->ports, PnvPortRange, 0).first == g_array_index(h->ports, PnvPortRange, 0).last) {
        json_object_set_int_member(outbound, "server_port", g_array_index(h->ports, PnvPortRange, 0).first);
        return;
    }
    ranges = json_array_new();
    for (guint i = 0; i < h->ports->len; i++) {
        PnvPortRange r = g_array_index(h->ports, PnvPortRange, i);
        g_autofree char *text = g_strdup_printf("%u:%u", r.first, r.last);
        json_array_add_string_element(ranges, text);
    }
    json_object_set_array_member(outbound, "server_ports", ranges);
    interval = g_strdup_printf("%us", h->hop_interval);
    set_string(outbound, "hop_interval", interval);
}

char *pnv_sb_hysteria2_config(JsonNode *template, const PnvProfile *profile, const PnvSbTarget *target,
                              GError **error)
{
    const PnvHysteria2 *h = &profile->hysteria2;
    g_autoptr(JsonNode) root = NULL;
    JsonObject *tun, *outbound, *tls, *obfs;
    JsonArray *address;

    if (h->tls_pin_sha256) {
        /* sing-box 1.14 pins the public key (certificate_public_key_sha256), Hysteria2 links the certificate. */
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "tls-pin-sha256 is not supported: sing-box pins the public key, not the certificate");
        return NULL;
    }
    if (!JSON_NODE_HOLDS_OBJECT(template)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the template is not a JSON object");
        return NULL;
    }
    root = json_node_copy(template);
    tun = json_array_get_object_element(json_object_get_array_member(json_node_get_object(root), "inbounds"), 0);
    outbound = json_array_get_object_element(json_object_get_array_member(json_node_get_object(root), "outbounds"), 0);
    tls = object(outbound, "tls");

    set_string(tun, "interface_name", target->tun);
    address = json_array_new();
    json_array_add_string_element(address, target->tun_cidr);
    json_object_set_array_member(tun, "address", address);
    json_object_set_int_member(tun, "mtu", target->mtu);

    set_string(outbound, "server", target->server_ip);
    set_ports(outbound, h);
    set_string(outbound, "password", h->password);
    if (h->up_mbps)
        json_object_set_int_member(outbound, "up_mbps", h->up_mbps);
    if (h->down_mbps)
        json_object_set_int_member(outbound, "down_mbps", h->down_mbps);
    if (h->obfs_salamander) {
        obfs = json_object_new();
        set_string(obfs, "type", "salamander");
        set_string(obfs, "password", h->obfs_password);
        json_object_set_object_member(outbound, "obfs", obfs);
    }
    set_string(tls, "server_name", h->tls_sni ? h->tls_sni : profile->server);
    if (h->tls_alpn)
        json_object_set_array_member(tls, "alpn", strings(h->tls_alpn));
    if (h->tls_insecure)
        json_object_set_boolean_member(tls, "insecure", TRUE);
    if (target->parent)
        set_string(outbound, "bind_interface", target->parent);
    if (profile->extra)
        merge(outbound, profile->extra);
    return json_to_string(root, TRUE);
}

JsonNode *pnv_sb_load_template(const char *path, GError **error)
{
    g_autoptr(JsonParser) parser = json_parser_new();

    if (!json_parser_load_from_file(parser, path, error))
        return NULL;
    return json_node_copy(json_parser_get_root(parser));
}
