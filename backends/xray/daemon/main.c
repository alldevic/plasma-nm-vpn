/* SPDX-License-Identifier: MIT */
/* The daemon for the protocols of the xray core: VLESS XHTTP Reality. xray gets the daemon's TUN as fd 3 through
 * plasma-nm-vpn-tun-fd, so it needs no capabilities and leaves the link alone. */
#include <string.h>

#include "config.h"
#include "pnv-daemon.h"

#define DATA_DIR "/usr/share/plasma-nm-vpn/xray"
#define TUN_FD "/usr/lib/plasma-nm-vpn/plasma-nm-vpn-tun-fd"

static gboolean vless_connect(PnvSession *session, GError **error)
{
    const char *args[] = {pnv_session_tun(session), "XRAY_TUN_FD", PNV_CORE_DIR "/xray", "run", "-c",
                          "${CREDENTIALS_DIRECTORY}/config.json", NULL};
    const PnvCore xray = {.binary = TUN_FD, .args = args, .capabilities = 0};
    g_autoptr(JsonNode) template = pnv_xray_load_template(DATA_DIR "/vless.json", error);
    PnvXrayTarget target = {
        .tun = pnv_session_tun(session),
        .server_ip = pnv_session_server_ip(session),
        .parent = pnv_session_parent(session),
    };
    g_autofree char *config = template ? pnv_xray_vless_config(template, pnv_session_profile(session), &target, error)
                                       : NULL;
    gboolean ok = config && pnv_session_start_core(session, &xray, config, error);

    if (config)
        explicit_bzero(config, strlen(config));
    return ok;
}

static const PnvListFile lists[] = {
    {"extra/", DATA_DIR "/vless-extra.list"},
    {"xhttp-extra/", DATA_DIR "/xhttp-extra.list"},
    {"tls-fingerprint/", DATA_DIR "/tls-fingerprint.list"},
    {"xhttp-mode/", DATA_DIR "/xhttp-mode.list"},
    {NULL},
};

static const PnvProtocol vless = {
    .service = PNV_SERVICE_VLESS,
    .name = "vless",
    .lists = lists,
    .connect = vless_connect,
};

static const PnvProtocol *const protocols[] = {&vless, NULL};

int main(int argc, char **argv)
{
    return pnv_daemon_main(argc, argv, protocols);
}
