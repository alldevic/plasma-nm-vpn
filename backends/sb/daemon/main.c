/* SPDX-License-Identifier: MIT */
/* The daemon for the protocols of the sing-box core. */
#include <linux/capability.h>
#include <string.h>

#include "config.h"
#include "pnv-daemon.h"

static const char *const singbox_args[] = {"run", "-c", "${CREDENTIALS_DIRECTORY}/config.json", NULL};

static const PnvCore singbox = {
    .binary = "sing-box",
    .args = singbox_args,
    .capabilities = 1ULL << CAP_NET_ADMIN,
};

#define TEMPLATE_DIR "/usr/share/plasma-nm-vpn/sb"

static gboolean hysteria2_connect(PnvSession *session, GError **error)
{
    g_autoptr(JsonNode) template = pnv_sb_load_template(TEMPLATE_DIR "/hysteria2.json", error);
    PnvSbTarget target = {
        .tun = pnv_session_tun(session),
        .tun_cidr = pnv_session_tun_cidr(session),
        .mtu = pnv_session_mtu(session),
        .server_ip = pnv_session_server_ip(session),
        .parent = pnv_session_parent(session),
    };
    g_autofree char *config = template ? pnv_sb_hysteria2_config(template, pnv_session_profile(session), &target, error)
                                       : NULL;
    gboolean ok = config && pnv_session_start_core(session, &singbox, config, error);

    if (config)
        explicit_bzero(config, strlen(config));
    return ok;
}

static const PnvProtocol hysteria2 = {
    .service = PNV_SERVICE_HYSTERIA2,
    .name = "hysteria2",
    .allowlist = TEMPLATE_DIR "/hysteria2-extra.list",
    .connect = hysteria2_connect,
};

static const PnvProtocol *const protocols[] = {&hysteria2, NULL};

int main(int argc, char **argv)
{
    return pnv_daemon_main(argc, argv, protocols);
}
