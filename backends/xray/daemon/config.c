/* SPDX-License-Identifier: MIT */
/* The xray config for VLESS XHTTP Reality: the template for the pinned xray version, filled from the validated
 * profile and the session. extra goes into the outbound, xhttp-extra into xhttpSettings.extra. */
#include "config.h"

static JsonObject *member(JsonObject *parent, const char *name)
{
    return json_object_get_object_member(parent, name);
}

static void merge(JsonObject *into, JsonObject *from)
{
    JsonObjectIter iter;
    const char *name;
    JsonNode *node;

    json_object_iter_init(&iter, from);
    while (json_object_iter_next(&iter, &name, &node)) {
        if (JSON_NODE_HOLDS_OBJECT(node) && json_object_has_member(into, name) &&
            JSON_NODE_HOLDS_OBJECT(json_object_get_member(into, name)))
            merge(member(into, name), json_node_get_object(node));
        else
            json_object_set_member(into, name, json_node_copy(node));
    }
}

char *pnv_xray_vless_config(JsonNode *template, const PnvProfile *profile, const PnvXrayTarget *target,
                            GError **error)
{
    const PnvVless *v = &profile->vless;
    g_autoptr(JsonNode) root = NULL;
    JsonObject *outbound, *server, *user, *stream, *xhttp, *reality;

    if (!JSON_NODE_HOLDS_OBJECT(template) || !target->parent) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            target->parent ? "the template is not a JSON object"
                                           : "no parent device for sockopt.interface");
        return NULL;
    }
    root = json_node_copy(template);
    json_object_set_string_member(
        member(json_array_get_object_element(json_object_get_array_member(json_node_get_object(root), "inbounds"), 0),
               "settings"),
        "name", target->tun);
    outbound = json_array_get_object_element(json_object_get_array_member(json_node_get_object(root), "outbounds"), 0);
    server = json_array_get_object_element(json_object_get_array_member(member(outbound, "settings"), "vnext"), 0);
    user = json_array_get_object_element(json_object_get_array_member(server, "users"), 0);
    stream = member(outbound, "streamSettings");
    xhttp = member(stream, "xhttpSettings");
    reality = member(stream, "realitySettings");

    json_object_set_string_member(server, "address", target->server_ip);
    json_object_set_int_member(server, "port", v->port);
    json_object_set_string_member(user, "id", v->uuid);
    json_object_set_string_member(user, "encryption", v->encryption);

    /* SNI and Host stay names even though the address is an IP. */
    json_object_set_string_member(xhttp, "host", v->xhttp_host ? v->xhttp_host
                                                 : g_hostname_is_ip_address(profile->server) ? v->tls_sni
                                                                                             : profile->server);
    json_object_set_string_member(xhttp, "path", v->xhttp_path);
    json_object_set_string_member(xhttp, "mode", v->xhttp_mode);
    if (v->xhttp_extra)
        json_object_set_object_member(xhttp, "extra", json_object_ref(v->xhttp_extra));

    json_object_set_string_member(reality, "serverName", v->tls_sni);
    json_object_set_string_member(reality, "fingerprint", v->tls_fingerprint);
    json_object_set_string_member(reality, "password", v->reality_pbk);
    json_object_set_string_member(reality, "shortId", v->reality_sid ? v->reality_sid : "");
    if (v->reality_spx)
        json_object_set_string_member(reality, "spiderX", v->reality_spx);
    json_object_set_string_member(member(stream, "sockopt"), "interface", target->parent);

    if (profile->extra)
        merge(outbound, profile->extra);
    return json_to_string(root, TRUE);
}

JsonNode *pnv_xray_load_template(const char *path, GError **error)
{
    g_autoptr(JsonParser) parser = json_parser_new();

    if (!json_parser_load_from_file(parser, path, error))
        return NULL;
    return json_node_copy(json_parser_get_root(parser));
}
