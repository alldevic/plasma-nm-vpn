/* SPDX-License-Identifier: MIT */
/* The daemon for the protocols of the sing-box core. */
#include <linux/capability.h>
#include <string.h>

#include "pnv-daemon.h"

static const char *const singbox_args[] = {"run", "-c", "${CREDENTIALS_DIRECTORY}/config.json", NULL};

static const PnvCore singbox = {
    .binary = "sing-box",
    .args = singbox_args,
    .capabilities = 1ULL << CAP_NET_ADMIN,
};

/* Temporary until the generator and template of 2.5: built with json-glib, never by pasting strings. */
static char *hysteria2_config(PnvSession *session)
{
    const PnvProfile *profile = pnv_session_profile(session);
    const PnvHysteria2 *h = &profile->hysteria2;
    g_autoptr(JsonBuilder) b = json_builder_new();
    g_autoptr(JsonNode) root = NULL;

    json_builder_begin_object(b);
    json_builder_set_member_name(b, "log");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "level");
    json_builder_add_string_value(b, "info");
    json_builder_end_object(b);

    json_builder_set_member_name(b, "inbounds");
    json_builder_begin_array(b);
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "type");
    json_builder_add_string_value(b, "tun");
    json_builder_set_member_name(b, "interface_name");
    json_builder_add_string_value(b, pnv_session_tun(session));
    json_builder_set_member_name(b, "address");
    json_builder_begin_array(b);
    json_builder_add_string_value(b, pnv_session_tun_cidr(session));
    json_builder_end_array(b);
    json_builder_set_member_name(b, "mtu");
    json_builder_add_int_value(b, pnv_session_mtu(session));
    json_builder_set_member_name(b, "auto_route");
    json_builder_add_boolean_value(b, FALSE);
    json_builder_set_member_name(b, "stack");
    json_builder_add_string_value(b, "system");
    json_builder_end_object(b);
    json_builder_end_array(b);

    json_builder_set_member_name(b, "outbounds");
    json_builder_begin_array(b);
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "type");
    json_builder_add_string_value(b, "hysteria2");
    json_builder_set_member_name(b, "tag");
    json_builder_add_string_value(b, "proxy");
    json_builder_set_member_name(b, "server");
    json_builder_add_string_value(b, pnv_session_server_ip(session));
    json_builder_set_member_name(b, "server_port");
    json_builder_add_int_value(b, g_array_index(h->ports, PnvPortRange, 0).first);
    json_builder_set_member_name(b, "password");
    json_builder_add_string_value(b, h->password);
    if (h->obfs_salamander) {
        json_builder_set_member_name(b, "obfs");
        json_builder_begin_object(b);
        json_builder_set_member_name(b, "type");
        json_builder_add_string_value(b, "salamander");
        json_builder_set_member_name(b, "password");
        json_builder_add_string_value(b, h->obfs_password);
        json_builder_end_object(b);
    }
    json_builder_set_member_name(b, "tls");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "enabled");
    json_builder_add_boolean_value(b, TRUE);
    json_builder_set_member_name(b, "server_name");
    json_builder_add_string_value(b, h->tls_sni ? h->tls_sni : profile->server);
    json_builder_set_member_name(b, "insecure");
    json_builder_add_boolean_value(b, h->tls_insecure);
    if (h->tls_alpn) {
        json_builder_set_member_name(b, "alpn");
        json_builder_begin_array(b);
        for (guint i = 0; i < h->tls_alpn->len; i++)
            json_builder_add_string_value(b, h->tls_alpn->pdata[i]);
        json_builder_end_array(b);
    }
    json_builder_end_object(b);
    if (pnv_session_parent(session)) {
        json_builder_set_member_name(b, "bind_interface");
        json_builder_add_string_value(b, pnv_session_parent(session));
    }
    json_builder_end_object(b);
    json_builder_end_array(b);

    json_builder_set_member_name(b, "route");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "final");
    json_builder_add_string_value(b, "proxy");
    json_builder_end_object(b);
    json_builder_end_object(b);

    root = json_builder_get_root(b);
    return json_to_string(root, FALSE);
}

static gboolean hysteria2_connect(PnvSession *session, GError **error)
{
    g_autofree char *config = hysteria2_config(session);
    gboolean ok = pnv_session_start_core(session, &singbox, config, error);

    explicit_bzero(config, strlen(config));
    return ok;
}

static const PnvProtocol hysteria2 = {
    .service = PNV_SERVICE_HYSTERIA2,
    .name = "hysteria2",
    .connect = hysteria2_connect,
};

static const PnvProtocol *const protocols[] = {&hysteria2, NULL};

int main(int argc, char **argv)
{
    return pnv_daemon_main(argc, argv, protocols);
}
