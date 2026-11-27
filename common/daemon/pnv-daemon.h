/* SPDX-License-Identifier: MIT */
#ifndef PNV_DAEMON_H
#define PNV_DAEMON_H

#include <NetworkManager.h>
#include <net/if.h>

#include "pnv-profile.h"

/* One daemon process serves one VPN connection (supports-multiple-connections=true). */
typedef struct PnvSession PnvSession;

typedef void (*PnvCleanup)(PnvSession *session);

typedef struct {
    const char *service;           /* vpn.service-type, e.g. org.freedesktop.NetworkManager.pnv.hysteria2 */
    const char *name;              /* for the journal */
    const char *extra_allowlist;   /* the list file for extra; NULL: extra is refused */
    /* Called once the run directory, the guard unit and the TUN exist: generates the config and calls
     * pnv_session_start_core(). An error fails the connection. */
    gboolean (*connect)(PnvSession *session, GError **error);
} PnvProtocol;

/* Needed (its `when` key is set), not in vpn.secrets, and its <key>-flags do not say NOT_REQUIRED. */
gboolean pnv_secret_needed(NMSettingVpn *s_vpn, const PnvSecret *secret);

#define PNV_RUN_DIR "/run/plasma-nm-vpn"
#define PNV_CORE_DIR "/usr/lib/plasma-nm-vpn/bin"
#define PNV_GUARD "/usr/bin/plasma-nm-vpn-guard"
#define PNV_CORE_USER "plasma-nm-vpn"

/* systemd (units.c). Unit names: plasma-nm-vpn-{daemon,guard,core}-<uuid>.{scope,service}. */
GDBusConnection *pnv_bus(void);
char *pnv_unit_name(const char *kind, const char *uuid);
char *pnv_unit_path(const char *name);
gboolean pnv_unit_start(const char *name, GVariantBuilder *props, GError **error);
void pnv_unit_stop(const char *name);
void pnv_unit_reset_failed(const char *name);
char *pnv_unit_state(const char *name);
gboolean pnv_unit_down(const char *name); /* inactive, failed or gone */
gboolean pnv_unit_wait_down(const char *name, guint timeout_ms);
void pnv_prop(GVariantBuilder *b, const char *name, GVariant *value);
GVariant *pnv_command(const char *const *argv);

/* The persistent TUN (tun.c). */
void pnv_tun_name(const char *uuid, char name[IFNAMSIZ]);
gboolean pnv_tun_exists(const char *name);
gboolean pnv_tun_create(const char *name, guint mtu, GError **error);
void pnv_tun_delete(const char *name);
int pnv_tun_carrier(const char *name);

/* The run directory and --daemon-gone (rundir.c). */
gboolean pnv_uuid_valid(const char *uuid);
char *pnv_run_dir(const char *uuid);
void pnv_run_dir_remove(const char *path);
gboolean pnv_daemon_gone(const char *uuid);
char *pnv_tun_owner(const char *uuid);

/* The core unit, one description for all cores (core.c). The argument "${CREDENTIALS_DIRECTORY}/config.json"
 * names the config, which systemd expands at launch. */
typedef struct {
    const char *binary;                  /* in PNV_CORE_DIR */
    const char *const *args;
    guint64 capabilities;                /* ambient and bounding set */
    const char *const *read_write_paths; /* NULL-terminated, or NULL */
} PnvCore;

/* Writes the config, starts the core unit and waits until it has attached to the TUN. */
gboolean pnv_session_start_core(PnvSession *session, const PnvCore *core, const char *config, GError **error);
const char *pnv_session_tun(PnvSession *session);
const char *pnv_session_server_ip(PnvSession *session);
const char *pnv_session_tun_cidr(PnvSession *session);
guint pnv_session_mtu(PnvSession *session);

/* The daemon's main(): --bus-name, --debug, the plugin on the bus, the main loop. */
int pnv_daemon_main(int argc, char **argv, const PnvProtocol *const *protocols);

const char *pnv_version(void);

NMConnection *pnv_session_connection(PnvSession *session);
const char *pnv_session_uuid(PnvSession *session);
const PnvProfile *pnv_session_profile(PnvSession *session);

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
