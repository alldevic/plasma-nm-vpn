/* SPDX-License-Identifier: MIT */
/* Profile schema and validation, shared by the daemons, the editors and the import parsers: GLib and json-glib
 * only. Everything in a profile is untrusted; a profile either passes in full and becomes a PnvProfile, or is
 * rejected with a PnvProblem. */
#ifndef PNV_PROFILE_H
#define PNV_PROFILE_H

#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define PNV_SCHEMA_VERSION 1

#define PNV_SERVICE_HYSTERIA2 "org.freedesktop.NetworkManager.pnv.hysteria2"
#define PNV_SERVICE_AMNEZIAWG "org.freedesktop.NetworkManager.pnv.amneziawg"

/* Stable message codes: the daemon renders them for the journal, the editors through ki18n. Append only. */
typedef enum {
    PNV_OK = 0,
    PNV_E_UNKNOWN_SERVICE,    /* detail: the service type */
    PNV_E_SCHEMA_TOO_NEW,     /* max: the newest known schema */
    PNV_E_UNKNOWN_KEY,        /* key */
    PNV_E_UNKNOWN_SECRET,     /* key */
    PNV_E_MISSING,            /* key */
    PNV_E_NOT_UTF8,           /* key, position */
    PNV_E_CONTROL_CHAR,       /* key, position */
    PNV_E_TOO_LONG,           /* key, max */
    PNV_E_EMPTY,              /* key */
    PNV_E_BAD_HOST,           /* key, position */
    PNV_E_BAD_PORT,           /* key, min, max */
    PNV_E_BAD_PORTS,          /* key, position */
    PNV_E_BAD_PORT_RANGE,     /* key, position: start greater than end */
    PNV_E_BAD_NUMBER,         /* key */
    PNV_E_OUT_OF_RANGE,       /* key, min, max */
    PNV_E_BAD_BOOL,           /* key */
    PNV_E_BAD_ENUM,           /* key */
    PNV_E_BAD_BASE64,         /* key, position */
    PNV_E_BAD_HEX,            /* key, position */
    PNV_E_BAD_LENGTH,         /* key, min = max = the expected decoded length in bytes */
    PNV_E_BAD_IP,             /* key, position: the list index */
    PNV_E_BAD_CIDR,           /* key, position: the list index */
    PNV_E_TOO_MANY,           /* key, max */
    PNV_E_BAD_ALPN,           /* key, position: the list index */
    PNV_E_BAD_FLAGS,          /* key: the <key>-flags entry */
    PNV_E_BAD_JSON,           /* key */
    PNV_E_JSON_NOT_OBJECT,    /* key */
    PNV_E_JSON_TOO_DEEP,      /* key, max */
    PNV_E_EXTRA_FORBIDDEN,    /* key, detail: the JSON path */
    PNV_E_EXTRA_NOT_ALLOWED,  /* key, detail: the JSON path */
    PNV_E_EXTRA_BAD_VALUE,    /* key, detail: the JSON path */
    PNV_E_LIST_FILE,          /* detail: the file; the loader could not read it */
    PNV_E_LIST_ENTRY,         /* detail: the file, position: the line */
} PnvCode;

/* Never carries a value from the profile: only key names, JSON paths, positions and limits. */
typedef struct {
    PnvCode code;
    char *key;
    char *detail;
    gint64 position;
    gint64 min, max;
} PnvProblem;

void pnv_problem_clear(PnvProblem *problem);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(PnvProblem, pnv_problem_clear)
/* A stable name ("unknown-key") and an English sentence for the journal. */
const char *pnv_code_name(PnvCode code);
char *pnv_problem_describe(const PnvProblem *problem);

/* Lists as data (allowlists, value lists for the forms): one entry per line with optional fields, '#'
 * comments. */
typedef struct {
    GPtrArray *entries; /* in file order */
    GHashTable *set;
} PnvList;

PnvList *pnv_list_load(const char *path, PnvProblem *problem);
PnvList *pnv_list_parse(const char *text, const char *name, PnvProblem *problem);
gboolean pnv_list_contains(const PnvList *list, const char *entry);
/* The fields after the entry, NULL-terminated; NULL if the entry is not in the list. */
const char *const *pnv_list_fields(const PnvList *list, const char *entry);
void pnv_list_free(PnvList *list);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PnvList, pnv_list_free)

typedef struct {
    guint16 first, last;
} PnvPortRange;

typedef struct {
    guint32 address; /* host byte order */
    guint8 prefix;
} PnvCidr4;

typedef struct {
    GArray *ports; /* PnvPortRange; one entry with first == last for a single port */
    guint hop_interval;
    gboolean obfs_salamander;
    char *tls_sni;
    GPtrArray *tls_alpn;
    gboolean tls_insecure;
    GBytes *tls_pin_sha256;
    guint up_mbps, down_mbps; /* 0: not set */
    char *password;
    char *obfs_password;
} PnvHysteria2;

/* An AmneziaWG parameter from the allowlist: its key in the awg config and the checked value. */
typedef struct {
    char *key;
    char *value;
} PnvAwgParam;

typedef struct {
    guint16 port;
    GArray *address; /* PnvCidr4; the first one is the TUN address */
    char *peer_public_key;
    GArray *allowed_ips; /* PnvCidr4, normalized */
    guint keepalive;     /* 0: off */
    GPtrArray *params;   /* PnvAwgParam, in key order */
    char *private_key;
    char *preshared_key;
    char *header_protection_key;
} PnvAmneziawg;

typedef struct {
    const char *service;
    guint schema;
    char *server;
    GPtrArray *dns; /* IPv4 literals; empty: not set */
    gboolean kill_switch;
    gboolean allow_lan;
    guint mtu; /* 0: not set */
    JsonObject *extra; /* NULL: not set */
    PnvHysteria2 hysteria2;
    PnvAmneziawg amneziawg;
} PnvProfile;

/* The secrets of a service type, for need_secrets and the auth dialog. A secret with `when` is needed only if
 * that vpn.data key is set and not empty; an optional one only if the profile has its <key>-flags (and they do
 * not say NOT_REQUIRED). */
typedef struct {
    const char *key;
    const char *label;
    const char *when;
    gboolean optional;
} PnvSecret;

const PnvSecret *pnv_service_secrets(const char *service); /* NULL-terminated; NULL for unknown types */

/* vpn.data and vpn.secrets (string → string) → profile. The allowlist is the core version's: extra keys for
 * the proxy cores, awg-* parameters for AmneziaWG; NULL rejects all of those. */
PnvProfile *pnv_profile_parse(const char *service, GHashTable *data, GHashTable *secrets,
                              const PnvList *allowlist, PnvProblem *problem);
void pnv_profile_free(PnvProfile *profile);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PnvProfile, pnv_profile_free)

/* One field on its own, as the editor forms check it. */
gboolean pnv_check_field(const char *service, const char *key, const char *value, const PnvList *extra_allowed,
                         PnvProblem *problem);

/* The validators themselves; on failure they fill problem (with key) and return FALSE. */
gboolean pnv_check_string(const char *key, const char *value, gsize max, PnvProblem *problem);
gboolean pnv_check_host(const char *key, const char *value, PnvProblem *problem);
gboolean pnv_parse_port(const char *key, const char *value, guint16 *port, PnvProblem *problem);
gboolean pnv_parse_ports(const char *key, const char *value, GArray **ranges, PnvProblem *problem);
gboolean pnv_parse_uint(const char *key, const char *value, guint min, guint max, guint *out, PnvProblem *problem);
gboolean pnv_parse_bool(const char *key, const char *value, gboolean *out, PnvProblem *problem);
gboolean pnv_parse_base64(const char *key, const char *value, gsize length, GBytes **out, PnvProblem *problem);
gboolean pnv_parse_hex(const char *key, const char *value, gsize length, GBytes **out, PnvProblem *problem);
gboolean pnv_parse_ip4_list(const char *key, const char *value, guint max, GPtrArray **out, PnvProblem *problem);
/* Normalizes 0.0.0.0/1 + 128.0.0.0/1 to 0.0.0.0/0 and drops duplicates. */
gboolean pnv_parse_cidr4_list(const char *key, const char *value, guint max, GArray **out, PnvProblem *problem);
gboolean pnv_parse_alpn(const char *key, const char *value, GPtrArray **out, PnvProblem *problem);
gboolean pnv_parse_extra(const char *key, const char *value, const PnvList *allowed, JsonObject **out,
                         PnvProblem *problem);

G_END_DECLS

#endif
