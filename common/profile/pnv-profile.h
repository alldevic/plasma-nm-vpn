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
#define PNV_SERVICE_VLESS "org.freedesktop.NetworkManager.pnv.vless"

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
    PNV_E_BAD_PATH,           /* key: not an HTTP path (6.4) */
    PNV_E_BAD_ENCRYPTION,     /* key: neither none nor a VLESS Encryption key (6.4) */
    PNV_E_BAD_CHAIN,          /* key: a character an AmneziaWG tag chain cannot hold (6.4) */
    /* The import parsers (6.6): errors, then warnings. */
    PNV_E_IMPORT_FORMAT,      /* not a hysteria2://, hy2:// or vless:// link nor an AmneziaWG .conf */
    PNV_E_IMPORT_TOO_LARGE,   /* max: the limit in bytes */
    PNV_E_IMPORT_UNSUPPORTED, /* detail: the feature (gecko, ech, hysteria2+realm, pinSHA256) */
    PNV_E_IMPORT_BAD_LINK,    /* detail: the part of the link (scheme, host, port, path, query, encoding) */
    PNV_E_IMPORT_NOT_REALITY, /* key: type or security; a vless:// link needs type=xhttp and security=reality */
    PNV_E_IMPORT_PEERS,       /* a .conf with more than one [Peer] */
    PNV_E_IMPORT_BAD_LINE,    /* position: the line of the .conf */
    PNV_E_IMPORT_DUPLICATE,   /* key: given twice */
    PNV_W_DROPPED,            /* key: a parameter neither in the form nor on the extra allowlist, dropped */
    PNV_W_WG_QUICK,           /* key: a wg-quick key (hook, Table, SaveConfig), dropped */
    PNV_W_EXTRA_ADDRESS,      /* key, position: an IPv4 entry after the first, dropped (Ip4Config holds one) */
    PNV_W_IPV6,               /* key, position: an IPv6 entry, dropped (IPv6 in the tunnel is phase 2) */
    PNV_W_DNS_NAME,           /* key, position: a DNS entry that is not an IP address (a search domain), dropped */
    PNV_W_TO_EXTRA,           /* key: a parameter the form lacks, carried into extra by the allowlist (6.7) */
    /* The export (6.8). */
    PNV_W_EXPORT_LOST,        /* key: a field the format does not carry */
    PNV_W_EXPORT_NO_SECRET,   /* key: a secret asked every time, so not in the profile to export */
} PnvCode;

/* The codes are grouped by origin, each group a range: the validators (2.2), the import parsers (6.6) and the
 * export (6.8). */
#define PNV_CODES_VALIDATOR_FIRST PNV_E_UNKNOWN_SERVICE
#define PNV_CODES_VALIDATOR_LAST PNV_E_BAD_CHAIN
#define PNV_CODES_PARSER_FIRST PNV_E_IMPORT_FORMAT
#define PNV_CODES_PARSER_LAST PNV_W_TO_EXTRA
#define PNV_CODES_EXPORT_FIRST PNV_W_EXPORT_LOST
#define PNV_CODES_EXPORT_LAST PNV_W_EXPORT_NO_SECRET

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
typedef struct PnvList {
    GPtrArray *entries; /* in file order */
    GHashTable *set;
} PnvList;

PnvList *pnv_list_load(const char *path, PnvProblem *problem);
PnvList *pnv_list_parse(const char *text, const char *name, PnvProblem *problem);
gboolean pnv_list_contains(const PnvList *list, const char *entry);
/* The fields after the entry, NULL-terminated; NULL if the entry is not in the list. */
const char *const *pnv_list_fields(const PnvList *list, const char *entry);
void pnv_list_free(PnvList *list);
/* A protocol's lists are merged into one, each file under its prefix ("extra/", "awg/", "tls-fingerprint/"); the
 * schema fields look up their values under their own prefix. */
PnvList *pnv_list_new(void);
void pnv_list_merge(PnvList *into, const PnvList *from, const char *prefix);
gboolean pnv_list_merge_file(PnvList *into, const char *path, const char *prefix, PnvProblem *problem);
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
    guint skipped_ipv6; /* IPv6 entries dropped from address and allowed-ips (phase 2) */
} PnvAmneziawg;

typedef struct {
    guint16 port;
    char *encryption; /* "none" or a VLESS Encryption (ML-KEM) client string */
    char *reality_pbk;
    char *reality_sid; /* NULL: empty */
    char *reality_spx;
    char *tls_sni;
    char *tls_fingerprint;
    char *xhttp_mode;
    char *xhttp_path;
    char *xhttp_host; /* NULL: the server name */
    JsonObject *xhttp_extra;
    char *uuid;
} PnvVless;

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
    PnvVless vless;
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

/* vpn.data and vpn.secrets (string → string) → profile. The lists are the core version's, merged under their
 * prefixes (pnv_list_merge); NULL rejects everything that needs them. */
PnvProfile *pnv_profile_parse(const char *service, GHashTable *data, GHashTable *secrets,
                              const PnvList *allowlist, PnvProblem *problem);
/* The same checks without requiring the secrets: an imported link may come without them. */
gboolean pnv_profile_check(const char *service, GHashTable *data, GHashTable *secrets, const PnvList *allowlist,
                           PnvProblem *problem);
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
/* Like pnv_parse_cidr4_list, but valid IPv6 entries are skipped and counted instead of rejected. */
gboolean pnv_parse_cidr_list_v4(const char *key, const char *value, guint max, GArray **out, guint *skipped6,
                                PnvProblem *problem);
/* Normalizes 0.0.0.0/1 + 128.0.0.0/1 to 0.0.0.0/0 and drops duplicates. */
gboolean pnv_parse_cidr4_list(const char *key, const char *value, guint max, GArray **out, PnvProblem *problem);
gboolean pnv_parse_alpn(const char *key, const char *value, GPtrArray **out, PnvProblem *problem);
/* Member paths are looked up as prefix + path; an entry "a.*" allows any member name directly under a. */
gboolean pnv_parse_extra(const char *key, const char *value, const PnvList *allowed, const char *prefix, JsonObject **out,
                         PnvProblem *problem);

/* Import (6.6): a hysteria2:// or hy2:// link, a vless:// link or an AmneziaWG .conf → the profile's fields. The
 * result passes the validators above; what does not fit is dropped with a warning, never silently. The link's
 * #fragment is ignored: plasma-nm names the connection from its own field. */
#define PNV_IMPORT_MAX_SIZE (64 * 1024)

typedef struct {
    const char *service; /* PNV_SERVICE_* */
    GHashTable *data;    /* vpn.data, char * → char * */
    GHashTable *secrets; /* vpn.secrets; a link without secrets leaves them out */
    GArray *warnings;    /* PnvProblem, in input order */
} PnvImport;

/* The service a text imports as, by its form alone; NULL if it is none of the formats. */
const char *pnv_import_service(const char *text);
/* lists: the target service's lists merged under their prefixes, as its daemon loads them. */
PnvImport *pnv_import(const char *text, const PnvList *lists, PnvProblem *problem);
void pnv_import_free(PnvImport *import);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PnvImport, pnv_import_free)

/* Export (6.8): the profile as a hysteria2:// or vless:// link or an AmneziaWG .conf, which pnv_import() reads back.
 * secrets NULL: none are written. warnings (PnvProblem) names each set field the format does not carry and each
 * secret asked every time; flags keys and schema are the profile's own and not reported. lists: for the AmneziaWG
 * parameter names. */
char *pnv_export(const char *service, GHashTable *data, GHashTable *secrets, const PnvList *lists, GArray *warnings);

G_END_DECLS

#endif
