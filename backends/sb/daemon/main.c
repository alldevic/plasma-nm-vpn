/* SPDX-License-Identifier: MIT */
/* The daemon for the protocols of the sing-box core. */
#include "pnv-daemon.h"

static gboolean hysteria2_connect(PnvSession *session, GError **error G_GNUC_UNUSED)
{
    /* The core is launched from 2.3 on; until then the connection stays in STARTING. */
    pnv_message(session, "hysteria2: tunnel setup is not implemented yet");
    return TRUE;
}

static const PnvProtocol hysteria2 = {
    .service = "org.freedesktop.NetworkManager.pnv.hysteria2",
    .name = "hysteria2",
    .connect = hysteria2_connect,
};

static const PnvProtocol *const protocols[] = {&hysteria2, NULL};

int main(int argc, char **argv)
{
    return pnv_daemon_main(argc, argv, protocols);
}
