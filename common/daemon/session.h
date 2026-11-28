/* SPDX-License-Identifier: MIT */
/* The session internals, shared by plugin.c and core.c. */
#ifndef PNV_SESSION_H
#define PNV_SESSION_H

#include "pnv-daemon.h"

struct PnvSession {
    NMVpnServicePlugin *plugin;
    NMConnection *connection;
    const PnvProtocol *protocol;
    PnvProfile *profile;
    GPtrArray *cleanups;
    char *uuid;
    char *run_dir;
    char *scope, *guard, *core;
    char tun[IFNAMSIZ];
    char *server_ip;
    gboolean server_literal;
    guint32 network;  /* the reserved /30, host byte order */
    guint32 tun_address;
    guint tun_prefix;
    char tun_cidr[24];
    char *parent;
    NMClient *client;
    NMActiveConnection *ac;
    guint core_watch;
    gboolean core_ready;
    char *test_hook;
    gboolean kill_switch;          /* the kill switch chains were installed by this session */
    gboolean inherited;            /* working under the chains of an earlier session, until success */
    gboolean give_up;              /* set before nm_vpn_service_plugin_failure, which calls disconnect */
    guint deactivation_reason;     /* NMActiveConnectionStateReason seen on the VPN connection, 0: none */
};

/* Steps 1 and 2 of Connect (design.md, "Core supervision and cleanup"), then resolution and the TUN. */
gboolean pnv_session_prepare(PnvSession *s, GError **error);
void pnv_session_send_config(PnvSession *s);
void pnv_session_succeeded(PnvSession *s);

#endif
