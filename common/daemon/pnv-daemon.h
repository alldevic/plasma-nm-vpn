/* SPDX-License-Identifier: MIT */
#ifndef PNV_DAEMON_H
#define PNV_DAEMON_H

#include <NetworkManager.h>

/* One daemon process serves one VPN connection (supports-multiple-connections=true). */
typedef struct PnvSession PnvSession;

typedef void (*PnvCleanup)(PnvSession *session);

typedef struct {
    const char *service;           /* vpn.service-type, e.g. org.freedesktop.NetworkManager.pnv.hysteria2 */
    const char *name;              /* for the journal */
    /* Starts the connection. Errors before the core is launched go to error; later ones through pnv_give_up(). */
    gboolean (*connect)(PnvSession *session, GError **error);
} PnvProtocol;

/* The secrets of a service type, shared with the auth dialog; NULL-terminated, NULL for unknown types. */
typedef struct {
    const char *key;
    const char *label;
} PnvSecret;

const PnvSecret *pnv_service_secrets(const char *service);
/* Not in vpn.secrets and its <key>-flags do not say NOT_REQUIRED. */
gboolean pnv_secret_missing(NMSettingVpn *s_vpn, const char *key);

/* The daemon's main(): --bus-name, --debug, the plugin on the bus, the main loop. */
int pnv_daemon_main(int argc, char **argv, const PnvProtocol *const *protocols);

const char *pnv_version(void);

NMConnection *pnv_session_connection(PnvSession *session);
const char *pnv_session_uuid(PnvSession *session);

/* Registers a cleanup step; disconnect runs the steps synchronously, last registered first. */
void pnv_session_add_cleanup(PnvSession *session, const char *name, PnvCleanup step);

/* Gives up: logs the reason (never secrets) and reports the failure code to NM, which then stops the
 * connection and calls disconnect. */
void pnv_give_up(PnvSession *session, NMVpnPluginFailure code, const char *fmt, ...) G_GNUC_PRINTF(3, 4);

/* Journal messages carry the profile UUID. */
void pnv_log(PnvSession *session, GLogLevelFlags level, const char *fmt, ...) G_GNUC_PRINTF(3, 4);
#define pnv_message(session, ...) pnv_log(session, G_LOG_LEVEL_MESSAGE, __VA_ARGS__)
#define pnv_warning(session, ...) pnv_log(session, G_LOG_LEVEL_WARNING, __VA_ARGS__)
#define pnv_debug(session, ...)   pnv_log(session, G_LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif
