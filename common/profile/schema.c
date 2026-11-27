/* SPDX-License-Identifier: MIT */
/* Schema 1: the vpn.data and vpn.secrets keys of each service type (design.md, "Profile data schema"). */
#include <stddef.h>
#include <string.h>

#include "pnv-profile.h"

typedef enum {
    T_UINT,
    T_BOOL,
    T_HOST,
    T_SNI,
    T_PORTS,
    T_OBFS,
    T_ALPN,
    T_SHA256_HEX,
    T_IP4_LIST,
    T_SECRET,
    T_EXTRA,
    T_PORT,
    T_KEY32,
    T_CIDR4_LIST,
    T_AWG_PARAM,
    T_ENUM_LIST,
    T_B64URL32,
    T_SHORT_ID,
    T_URL_PATH,
    T_VLESS_ENCRYPTION,
    T_UUID,
} FieldType;

typedef struct {
    const char *key;
    FieldType type;
    gsize offset;
    gboolean required;
    guint min, max; /* T_UINT bounds; T_SECRET maximum length; T_CIDR4_LIST maximum entries */
    const char *list; /* T_EXTRA, T_ENUM_LIST: the prefix of their entries in the protocol's lists */
} Field;

#define AT(member) offsetof(PnvProfile, member)

static const Field common_fields[] = {
    {"server", T_HOST, AT(server), TRUE},
    {"dns", T_IP4_LIST, AT(dns), FALSE, 0, 8},
    {"kill-switch", T_BOOL, AT(kill_switch)},
    {"allow-lan", T_BOOL, AT(allow_lan)},
    {"mtu", T_UINT, AT(mtu), FALSE, 1280, 9000},
    {"extra", T_EXTRA, AT(extra), .list = "extra/"},
    {NULL},
};

static const Field hysteria2_fields[] = {
    {"ports", T_PORTS, AT(hysteria2.ports), TRUE},
    {"hop-interval", T_UINT, AT(hysteria2.hop_interval), FALSE, 5, 3600},
    {"obfs", T_OBFS, AT(hysteria2.obfs_salamander)},
    {"tls-sni", T_SNI, AT(hysteria2.tls_sni)},
    {"tls-alpn", T_ALPN, AT(hysteria2.tls_alpn)},
    {"tls-insecure", T_BOOL, AT(hysteria2.tls_insecure)},
    {"tls-pin-sha256", T_SHA256_HEX, AT(hysteria2.tls_pin_sha256)},
    {"up-mbps", T_UINT, AT(hysteria2.up_mbps), FALSE, 1, 100000},
    {"down-mbps", T_UINT, AT(hysteria2.down_mbps), FALSE, 1, 100000},
    {NULL},
};

static const Field hysteria2_secret_fields[] = {
    {"password", T_SECRET, AT(hysteria2.password), FALSE, 1, 1024},
    {"obfs-password", T_SECRET, AT(hysteria2.obfs_password), FALSE, 1, 1024},
    {NULL},
};

static const PnvSecret hysteria2_secrets[] = {
    {"password", "Password", NULL},
    {"obfs-password", "Obfuscation password", "obfs"},
    {NULL},
};

static const Field amneziawg_fields[] = {
    {"port", T_PORT, AT(amneziawg.port), TRUE},
    {"address", T_CIDR4_LIST, AT(amneziawg.address), TRUE, 0, 8},
    {"peer-public-key", T_KEY32, AT(amneziawg.peer_public_key), TRUE},
    {"allowed-ips", T_CIDR4_LIST, AT(amneziawg.allowed_ips), TRUE, 0, 64},
    {"keepalive", T_UINT, AT(amneziawg.keepalive), FALSE, 0, 65535},
    {NULL},
};

static const Field amneziawg_secret_fields[] = {
    {"private-key", T_KEY32, AT(amneziawg.private_key)},
    {"preshared-key", T_KEY32, AT(amneziawg.preshared_key)},
    {"awg-headerprotectionkey", T_KEY32, AT(amneziawg.header_protection_key)},
    {NULL},
};

static const PnvSecret amneziawg_secrets[] = {
    {"private-key", "Private key", NULL, FALSE},
    {"preshared-key", "Preshared key", NULL, TRUE},
    {"awg-headerprotectionkey", "Header protection key", NULL, TRUE},
    {NULL},
};

static const Field vless_fields[] = {
    {"port", T_PORT, AT(vless.port), TRUE},
    {"encryption", T_VLESS_ENCRYPTION, AT(vless.encryption)},
    {"reality-pbk", T_B64URL32, AT(vless.reality_pbk), TRUE},
    {"reality-sid", T_SHORT_ID, AT(vless.reality_sid)},
    {"reality-spx", T_URL_PATH, AT(vless.reality_spx)},
    {"tls-sni", T_SNI, AT(vless.tls_sni), TRUE},
    {"tls-fingerprint", T_ENUM_LIST, AT(vless.tls_fingerprint), .list = "tls-fingerprint/"},
    {"xhttp-mode", T_ENUM_LIST, AT(vless.xhttp_mode), .list = "xhttp-mode/"},
    {"xhttp-path", T_URL_PATH, AT(vless.xhttp_path)},
    {"xhttp-host", T_SNI, AT(vless.xhttp_host)},
    {"xhttp-extra", T_EXTRA, AT(vless.xhttp_extra), .list = "xhttp-extra/"},
    {NULL},
};

static const Field vless_secret_fields[] = {
    {"uuid", T_UUID, AT(vless.uuid)},
    {NULL},
};

static const PnvSecret vless_secrets[] = {
    {"uuid", "User ID (UUID)", NULL, FALSE},
    {NULL},
};

/* awg-<parameter> keys, checked against the list entry "awg/awg-<parameter> <config key> <type>". */
static const Field awg_param = {NULL, T_AWG_PARAM, AT(amneziawg.params)};

typedef struct {
    const char *service;
    const Field *fields;
    const Field *secret_fields;
    const PnvSecret *secrets;
    const char *param_prefix; /* data keys with this prefix are allowlisted parameters */
} Protocol;

static const Protocol protocols[] = {
    {PNV_SERVICE_HYSTERIA2, hysteria2_fields, hysteria2_secret_fields, hysteria2_secrets, NULL},
    {PNV_SERVICE_AMNEZIAWG, amneziawg_fields, amneziawg_secret_fields, amneziawg_secrets, "awg-"},
    {PNV_SERVICE_VLESS, vless_fields, vless_secret_fields, vless_secrets, NULL},
};

/* Upgrades vpn.data in place from schema `from` to from + 1; entry i handles schema i + 1. None yet. */
typedef void (*Migration)(GHashTable *data);
static const Migration migrations[] = {NULL};

static gboolean fail(PnvProblem *p, PnvCode code, const char *key, const char *detail, gint64 min, gint64 max)
{
    if (p) {
        pnv_problem_clear(p);
        *p = (PnvProblem){code, g_strdup(key), g_strdup(detail), -1, min, max};
    }
    return FALSE;
}

static const Protocol *find_protocol(const char *service)
{
    for (gsize i = 0; i < G_N_ELEMENTS(protocols); i++)
        if (g_strcmp0(protocols[i].service, service) == 0)
            return &protocols[i];
    return NULL;
}

const PnvSecret *pnv_service_secrets(const char *service)
{
    const Protocol *protocol = find_protocol(service);
    return protocol ? protocol->secrets : NULL;
}

static const Field *find_in(const Field *fields, const char *key)
{
    for (const Field *f = fields; f->key; f++)
        if (strcmp(f->key, key) == 0)
            return f;
    return NULL;
}

static const Field *find_data_field(const Protocol *protocol, const char *key, const PnvList *allowlist)
{
    const Field *f = find_in(common_fields, key);

    if (!f)
        f = find_in(protocol->fields, key);
    if (!f && protocol->param_prefix && g_str_has_prefix(key, protocol->param_prefix)) {
        g_autofree char *entry = g_strconcat("awg/", key, NULL);
        if (pnv_list_contains(allowlist, entry))
            f = &awg_param;
    }
    return f;
}

static void param_free(gpointer data)
{
    PnvAwgParam *param = data;
    g_free(param->key);
    g_free(param->value);
    g_free(param);
}

/* N or N-M within 0..max, N <= M. */
static gboolean parse_range(const char *key, const char *value, guint max, PnvProblem *p)
{
    g_auto(GStrv) ends = g_strsplit(value, "-", 3);
    guint lo, hi;

    if (!ends[0] || (ends[1] && ends[2]))
        return fail(p, PNV_E_BAD_NUMBER, key, NULL, 0, max);
    if (!pnv_parse_uint(key, ends[0], 0, max, &lo, p) || (ends[1] && !pnv_parse_uint(key, ends[1], 0, max, &hi, p)))
        return FALSE;
    if (ends[1] && hi < lo)
        return fail(p, PNV_E_BAD_PORT_RANGE, key, NULL, 0, max);
    return TRUE;
}

/* An X25519 public key as Xray writes it: base64url without padding, 43 characters. */
static gboolean check_b64url32(const char *key, const char *value, PnvProblem *p)
{
    g_autofree char *std = g_strdup(value);
    g_autofree char *padded = NULL;

    if (strlen(value) != 43)
        return fail(p, PNV_E_BAD_LENGTH, key, NULL, 32, 32);
    for (char *c = std; *c; c++) {
        if (*c == '-')
            *c = '+';
        else if (*c == '_')
            *c = '/';
        else if (*c == '+' || *c == '/')
            return fail(p, PNV_E_BAD_BASE64, key, NULL, 0, 0);
    }
    padded = g_strconcat(std, "=", NULL);
    return pnv_parse_base64(key, padded, 32, NULL, p);
}

static gboolean valid_uuid(const char *s)
{
    static const char shape[] = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";

    if (strlen(s) != sizeof shape - 1)
        return FALSE;
    for (gsize i = 0; shape[i]; i++)
        if (shape[i] == '-' ? s[i] != '-' : !g_ascii_isxdigit(s[i]))
            return FALSE;
    return TRUE;
}

/* Types of the AmneziaWG allowlist (amneziawg-tools v3.1 src/config.c and type.c). */
static gboolean check_param(const char *key, const char *type, const char *value, PnvProblem *p)
{
    guint n;

    if (g_strcmp0(type, "uint16") == 0)
        return pnv_parse_uint(key, value, 0, 65535, &n, p);
    if (g_strcmp0(type, "u16range") == 0)
        return parse_range(key, value, 65535, p);
    if (g_strcmp0(type, "u32range") == 0)
        return parse_range(key, value, G_MAXUINT32, p);
    if (g_strcmp0(type, "onoff") == 0)
        return g_str_equal(value, "on") || g_str_equal(value, "off") ? TRUE : fail(p, PNV_E_BAD_ENUM, key, NULL, 0, 0);
    if (g_strcmp0(type, "chain") == 0) {
        /* Tag chains like <b 0x...><r 16>: printable ASCII, parsed by amneziawg-go; no '#', which starts a
         * comment in the awg config, and no '=' or '[', which would read as another key or section. */
        for (const char *c = value; *c; c++)
            if ((guchar)*c < 0x20 || (guchar)*c > 0x7e || strchr("#=[]", *c))
                return fail(p, PNV_E_BAD_ENUM, key, NULL, 0, 0);
        return TRUE;
    }
    return fail(p, PNV_E_LIST_ENTRY, key, NULL, 0, 0);
}

/* "<secret>-flags": NM's NMSettingSecretFlags, 0–7. */
static const Field *flags_of(const Protocol *protocol, const char *key)
{
    g_autofree char *base = g_str_has_suffix(key, "-flags") ? g_strndup(key, strlen(key) - 6) : NULL;
    return base ? find_in(protocol->secret_fields, base) : NULL;
}

/* Validates one value (already known not to be empty) and stores it in the profile. */
static gboolean apply(const Field *f, const char *key, const char *value, const PnvList *allowlist,
                      PnvProfile *profile, PnvProblem *p)
{
    void *at = (char *)profile + f->offset;
    gsize max = f->type == T_EXTRA ? 8192
                : f->type == T_SECRET ? f->max
                : f->type == T_AWG_PARAM || f->type == T_VLESS_ENCRYPTION ? 4096 /* ML-KEM-768 keys: ~1.6 KB */
                : 255;

    if (!pnv_check_string(key, value, max, p))
        return FALSE;
    switch (f->type) {
    case T_UINT:
        return pnv_parse_uint(key, value, f->min, f->max, at, p);
    case T_BOOL:
        return pnv_parse_bool(key, value, at, p);
    case T_HOST:
        if (!pnv_check_host(key, value, p))
            return FALSE;
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_SNI:
        /* A name, never an address literal: TLS sends no SNI for those. */
        if (!pnv_check_host(key, value, p))
            return FALSE;
        if (g_hostname_is_ip_address(value))
            return fail(p, PNV_E_BAD_HOST, key, NULL, 0, 0);
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_PORTS:
        g_clear_pointer((GArray **)at, g_array_unref);
        return pnv_parse_ports(key, value, at, p);
    case T_OBFS:
        if (strcmp(value, "salamander") != 0)
            return fail(p, PNV_E_BAD_ENUM, key, NULL, 0, 0);
        *(gboolean *)at = TRUE;
        return TRUE;
    case T_ALPN:
        g_clear_pointer((GPtrArray **)at, g_ptr_array_unref);
        return pnv_parse_alpn(key, value, at, p);
    case T_SHA256_HEX:
        g_clear_pointer((GBytes **)at, g_bytes_unref);
        return pnv_parse_hex(key, value, 32, at, p);
    case T_IP4_LIST:
        g_clear_pointer((GPtrArray **)at, g_ptr_array_unref);
        return pnv_parse_ip4_list(key, value, f->max, at, p);
    case T_SECRET:
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_EXTRA:
        g_clear_pointer((JsonObject **)at, json_object_unref);
        return pnv_parse_extra(key, value, allowlist, f->list, at, p);
    case T_PORT:
        return pnv_parse_port(key, value, at, p);
    case T_KEY32:
        if (!pnv_parse_base64(key, value, 32, NULL, p))
            return FALSE;
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_CIDR4_LIST: {
        guint skipped;
        g_clear_pointer((GArray **)at, g_array_unref);
        if (!pnv_parse_cidr_list_v4(key, value, f->max, at, &skipped, p))
            return FALSE;
        profile->amneziawg.skipped_ipv6 += skipped;
        if (!(*(GArray **)at)->len)
            return fail(p, PNV_E_MISSING, key, NULL, 0, 0);
        return TRUE;
    }
    case T_ENUM_LIST: {
        g_autofree char *entry = g_strconcat(f->list, value, NULL);
        if (!pnv_list_contains(allowlist, entry))
            return fail(p, PNV_E_BAD_ENUM, key, NULL, 0, 0);
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    }
    case T_B64URL32:
        if (!check_b64url32(key, value, p))
            return FALSE;
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_SHORT_ID:
        if (strlen(value) > 16 || strlen(value) % 2 || strspn(value, "0123456789abcdefABCDEF") != strlen(value))
            return fail(p, PNV_E_BAD_HEX, key, NULL, 0, 16);
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_URL_PATH:
        /* An HTTP path: starts with '/', printable ASCII without spaces. */
        if (value[0] != '/')
            return fail(p, PNV_E_BAD_ENUM, key, NULL, 0, 0);
        for (const char *c = value; *c; c++)
            if ((guchar)*c <= 0x20 || (guchar)*c > 0x7e)
                return fail(p, PNV_E_BAD_ENUM, key, NULL, 0, 0);
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_VLESS_ENCRYPTION:
        /* "none", or the client string of VLESS Encryption: mlkem768x25519plus.<mode>.<rtt>[.<padding>...].<key> */
        if (!g_str_equal(value, "none") && !(g_str_has_prefix(value, "mlkem768x25519plus.") &&
                                             strspn(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                                                           "0123456789._-") == strlen(value)))
            return fail(p, PNV_E_BAD_ENUM, key, NULL, 0, 0);
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_UUID:
        if (!valid_uuid(value))
            return fail(p, PNV_E_BAD_HEX, key, NULL, 0, 0);
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_AWG_PARAM: {
        g_autofree char *entry = g_strconcat("awg/", key, NULL);
        const char *const *fields = pnv_list_fields(allowlist, entry);
        PnvAwgParam *param;

        if (!fields || !fields[0] || !fields[1])
            return fail(p, PNV_E_UNKNOWN_KEY, key, NULL, 0, 0);
        if (!check_param(key, fields[1], value, p))
            return FALSE;
        if (!*(GPtrArray **)at)
            *(GPtrArray **)at = g_ptr_array_new_with_free_func(param_free);
        param = g_new(PnvAwgParam, 1);
        *param = (PnvAwgParam){g_strdup(fields[0]), g_strdup(value)};
        g_ptr_array_add(*(GPtrArray **)at, param);
        return TRUE;
    }
    }
    g_assert_not_reached();
}

static PnvProfile *profile_new(const char *service)
{
    PnvProfile *profile = g_new0(PnvProfile, 1);

    profile->service = service;
    profile->schema = PNV_SCHEMA_VERSION;
    profile->allow_lan = TRUE;
    profile->hysteria2.hop_interval = 30;
    profile->vless.encryption = g_strdup("none");
    profile->vless.tls_fingerprint = g_strdup("chrome");
    profile->vless.xhttp_mode = g_strdup("auto");
    profile->vless.xhttp_path = g_strdup("/");
    return profile;
}

/* Secrets are wiped before they are freed. */
static void wipe(char *secret)
{
    if (secret)
        explicit_bzero(secret, strlen(secret));
    g_free(secret);
}

void pnv_profile_free(PnvProfile *profile)
{
    if (!profile)
        return;
    g_free(profile->server);
    g_clear_pointer(&profile->dns, g_ptr_array_unref);
    g_clear_pointer(&profile->extra, json_object_unref);
    g_clear_pointer(&profile->hysteria2.ports, g_array_unref);
    g_free(profile->hysteria2.tls_sni);
    g_clear_pointer(&profile->hysteria2.tls_alpn, g_ptr_array_unref);
    g_clear_pointer(&profile->hysteria2.tls_pin_sha256, g_bytes_unref);
    wipe(profile->hysteria2.password);
    wipe(profile->hysteria2.obfs_password);
    g_clear_pointer(&profile->amneziawg.address, g_array_unref);
    g_free(profile->amneziawg.peer_public_key);
    g_clear_pointer(&profile->amneziawg.allowed_ips, g_array_unref);
    g_clear_pointer(&profile->amneziawg.params, g_ptr_array_unref);
    wipe(profile->amneziawg.private_key);
    wipe(profile->amneziawg.preshared_key);
    wipe(profile->amneziawg.header_protection_key);
    g_free(profile->vless.encryption);
    g_free(profile->vless.reality_pbk);
    g_free(profile->vless.reality_sid);
    g_free(profile->vless.reality_spx);
    g_free(profile->vless.tls_sni);
    g_free(profile->vless.tls_fingerprint);
    g_free(profile->vless.xhttp_mode);
    g_free(profile->vless.xhttp_path);
    g_free(profile->vless.xhttp_host);
    g_clear_pointer(&profile->vless.xhttp_extra, json_object_unref);
    wipe(profile->vless.uuid);
    g_free(profile);
}

static gint compare_keys(gconstpointer a, gconstpointer b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* The keys in a fixed order, so that the first problem reported does not depend on hashing. */
static GPtrArray *sorted_keys(GHashTable *table)
{
    GPtrArray *keys = g_ptr_array_new();
    GHashTableIter iter;
    gpointer key;

    if (table) {
        g_hash_table_iter_init(&iter, table);
        while (g_hash_table_iter_next(&iter, &key, NULL))
            g_ptr_array_add(keys, key);
    }
    g_ptr_array_sort(keys, compare_keys);
    return keys;
}

static gboolean check_schema(GHashTable *data, guint *schema, PnvProblem *p)
{
    const char *value = data ? g_hash_table_lookup(data, "schema") : NULL;

    *schema = PNV_SCHEMA_VERSION;
    if (!value || !*value)
        return TRUE;
    if (!pnv_check_string("schema", value, 16, p) || !pnv_parse_uint("schema", value, 1, G_MAXINT, schema, p))
        return FALSE;
    if (*schema > PNV_SCHEMA_VERSION)
        return fail(p, PNV_E_SCHEMA_TOO_NEW, "schema", NULL, 1, PNV_SCHEMA_VERSION);
    for (guint from = *schema; from < PNV_SCHEMA_VERSION; from++)
        migrations[from - 1](data);
    return TRUE;
}

/* Required fields are pointers (set means not NULL) or ports (set means not 0). */
static gboolean check_required(const Field *fields, PnvProfile *profile, PnvProblem *p)
{
    for (const Field *f = fields; f->key; f++) {
        void *at = (char *)profile + f->offset;
        if (f->required && (f->type == T_PORT ? *(guint16 *)at == 0 : *(void **)at == NULL))
            return fail(p, PNV_E_MISSING, f->key, NULL, 0, 0);
    }
    return TRUE;
}

PnvProfile *pnv_profile_parse(const char *service, GHashTable *data, GHashTable *secrets,
                              const PnvList *allowlist, PnvProblem *p)
{
    const Protocol *protocol = find_protocol(service);
    g_autoptr(PnvProfile) profile = NULL;
    g_autoptr(GPtrArray) data_keys = NULL, secret_keys = NULL;
    guint schema;

    if (!protocol) {
        fail(p, PNV_E_UNKNOWN_SERVICE, NULL, service, 0, 0);
        return NULL;
    }
    if (!check_schema(data, &schema, p))
        return NULL;
    profile = profile_new(protocol->service);
    data_keys = sorted_keys(data);
    for (guint i = 0; i < data_keys->len; i++) {
        const char *key = data_keys->pdata[i], *value = g_hash_table_lookup(data, key);
        const Field *f;
        guint flags;

        if (strcmp(key, "schema") == 0)
            continue;
        if (flags_of(protocol, key)) {
            if (!pnv_check_string(key, value, 16, p))
                return NULL;
            if (!pnv_parse_uint(key, value, 0, 7, &flags, NULL)) {
                fail(p, PNV_E_BAD_FLAGS, key, NULL, 0, 7);
                return NULL;
            }
            continue;
        }
        f = find_data_field(protocol, key, allowlist);
        if (!f) {
            fail(p, PNV_E_UNKNOWN_KEY, key, NULL, 0, 0);
            return NULL;
        }
        if (*value && !apply(f, key, value, allowlist, profile, p))
            return NULL;
    }
    secret_keys = sorted_keys(secrets);
    for (guint i = 0; i < secret_keys->len; i++) {
        const char *key = secret_keys->pdata[i], *value = g_hash_table_lookup(secrets, key);
        const Field *f = find_in(protocol->secret_fields, key);

        if (!f) {
            fail(p, PNV_E_UNKNOWN_SECRET, key, NULL, 0, 0);
            return NULL;
        }
        if (*value && !apply(f, key, value, NULL, profile, p))
            return NULL;
    }
    profile->schema = schema;
    if (!check_required(common_fields, profile, p) || !check_required(protocol->fields, profile, p))
        return NULL;
    for (const PnvSecret *s = protocol->secrets; s->key; s++) {
        const char *when = s->when && data ? g_hash_table_lookup(data, s->when) : NULL;
        const Field *f = find_in(protocol->secret_fields, s->key);

        if (!s->optional && (!s->when || (when && *when)) && !*(char **)((char *)profile + f->offset)) {
            fail(p, PNV_E_MISSING, s->key, NULL, 0, 0);
            return NULL;
        }
    }
    return g_steal_pointer(&profile);
}

gboolean pnv_check_field(const char *service, const char *key, const char *value, const PnvList *allowlist,
                         PnvProblem *p)
{
    const Protocol *protocol = find_protocol(service);
    g_autoptr(PnvProfile) scratch = NULL;
    const Field *f;
    guint n;

    if (!protocol)
        return fail(p, PNV_E_UNKNOWN_SERVICE, NULL, service, 0, 0);
    if (strcmp(key, "schema") == 0)
        return pnv_check_string(key, value, 16, p) && pnv_parse_uint(key, value, 1, PNV_SCHEMA_VERSION, &n, p);
    f = find_data_field(protocol, key, allowlist);
    if (!f)
        f = find_in(protocol->secret_fields, key);
    if (!f)
        return fail(p, PNV_E_UNKNOWN_KEY, key, NULL, 0, 0);
    if (!*value)
        return f->required ? fail(p, PNV_E_MISSING, key, NULL, 0, 0) : TRUE;
    scratch = profile_new(protocol->service);
    return apply(f, key, value, allowlist, scratch, p);
}
