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
} FieldType;

typedef struct {
    const char *key;
    FieldType type;
    gsize offset;
    gboolean required;
    guint min, max; /* T_UINT bounds; T_SECRET maximum length */
} Field;

#define AT(member) offsetof(PnvProfile, member)

static const Field common_fields[] = {
    {"server", T_HOST, AT(server), TRUE},
    {"dns", T_IP4_LIST, AT(dns), FALSE, 0, 8},
    {"kill-switch", T_BOOL, AT(kill_switch)},
    {"allow-lan", T_BOOL, AT(allow_lan)},
    {"mtu", T_UINT, AT(mtu), FALSE, 1280, 9000},
    {"extra", T_EXTRA, AT(extra)},
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

typedef struct {
    const char *service;
    const Field *fields;
    const Field *secret_fields;
    const PnvSecret *secrets;
} Protocol;

static const Protocol protocols[] = {
    {PNV_SERVICE_HYSTERIA2, hysteria2_fields, hysteria2_secret_fields, hysteria2_secrets},
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

static const Field *find_data_field(const Protocol *protocol, const char *key)
{
    const Field *f = find_in(common_fields, key);
    return f ? f : find_in(protocol->fields, key);
}

/* "<secret>-flags": NM's NMSettingSecretFlags, 0–7. */
static const Field *flags_of(const Protocol *protocol, const char *key)
{
    g_autofree char *base = g_str_has_suffix(key, "-flags") ? g_strndup(key, strlen(key) - 6) : NULL;
    return base ? find_in(protocol->secret_fields, base) : NULL;
}

/* Validates one value (already known not to be empty) and stores it in the profile. */
static gboolean apply(const Field *f, const char *value, const PnvList *extra_allowed, PnvProfile *profile,
                      PnvProblem *p)
{
    void *at = (char *)profile + f->offset;

    if (!pnv_check_string(f->key, value, f->type == T_EXTRA ? 8192 : f->type == T_SECRET ? f->max : 255, p))
        return FALSE;
    switch (f->type) {
    case T_UINT:
        return pnv_parse_uint(f->key, value, f->min, f->max, at, p);
    case T_BOOL:
        return pnv_parse_bool(f->key, value, at, p);
    case T_HOST:
        if (!pnv_check_host(f->key, value, p))
            return FALSE;
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_SNI:
        /* A name, never an address literal: TLS sends no SNI for those. */
        if (!pnv_check_host(f->key, value, p))
            return FALSE;
        if (g_hostname_is_ip_address(value))
            return fail(p, PNV_E_BAD_HOST, f->key, NULL, 0, 0);
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_PORTS:
        g_clear_pointer((GArray **)at, g_array_unref);
        return pnv_parse_ports(f->key, value, at, p);
    case T_OBFS:
        if (strcmp(value, "salamander") != 0)
            return fail(p, PNV_E_BAD_ENUM, f->key, NULL, 0, 0);
        *(gboolean *)at = TRUE;
        return TRUE;
    case T_ALPN:
        g_clear_pointer((GPtrArray **)at, g_ptr_array_unref);
        return pnv_parse_alpn(f->key, value, at, p);
    case T_SHA256_HEX:
        g_clear_pointer((GBytes **)at, g_bytes_unref);
        return pnv_parse_hex(f->key, value, 32, at, p);
    case T_IP4_LIST:
        g_clear_pointer((GPtrArray **)at, g_ptr_array_unref);
        return pnv_parse_ip4_list(f->key, value, f->max, at, p);
    case T_SECRET:
        g_free(*(char **)at);
        *(char **)at = g_strdup(value);
        return TRUE;
    case T_EXTRA:
        g_clear_pointer((JsonObject **)at, json_object_unref);
        return pnv_parse_extra(f->key, value, extra_allowed, at, p);
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
    return profile;
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
    if (profile->hysteria2.password)
        explicit_bzero(profile->hysteria2.password, strlen(profile->hysteria2.password));
    g_free(profile->hysteria2.password);
    if (profile->hysteria2.obfs_password)
        explicit_bzero(profile->hysteria2.obfs_password, strlen(profile->hysteria2.obfs_password));
    g_free(profile->hysteria2.obfs_password);
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

/* Required fields are all pointers: set means not NULL. */
static gboolean check_required(const Field *fields, PnvProfile *profile, PnvProblem *p)
{
    for (const Field *f = fields; f->key; f++)
        if (f->required && !*(void **)((char *)profile + f->offset))
            return fail(p, PNV_E_MISSING, f->key, NULL, 0, 0);
    return TRUE;
}

PnvProfile *pnv_profile_parse(const char *service, GHashTable *data, GHashTable *secrets,
                              const PnvList *extra_allowed, PnvProblem *p)
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
        f = find_data_field(protocol, key);
        if (!f) {
            fail(p, PNV_E_UNKNOWN_KEY, key, NULL, 0, 0);
            return NULL;
        }
        if (*value && !apply(f, value, extra_allowed, profile, p))
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
        if (*value && !apply(f, value, NULL, profile, p))
            return NULL;
    }
    profile->schema = schema;
    if (!check_required(common_fields, profile, p) || !check_required(protocol->fields, profile, p))
        return NULL;
    for (const PnvSecret *s = protocol->secrets; s->key; s++) {
        const char *when = s->when && data ? g_hash_table_lookup(data, s->when) : NULL;
        const Field *f = find_in(protocol->secret_fields, s->key);

        if ((!s->when || (when && *when)) && !*(char **)((char *)profile + f->offset)) {
            fail(p, PNV_E_MISSING, s->key, NULL, 0, 0);
            return NULL;
        }
    }
    return g_steal_pointer(&profile);
}

gboolean pnv_check_field(const char *service, const char *key, const char *value, const PnvList *extra_allowed,
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
    f = find_data_field(protocol, key);
    if (!f)
        f = find_in(protocol->secret_fields, key);
    if (!f)
        return fail(p, PNV_E_UNKNOWN_KEY, key, NULL, 0, 0);
    if (!*value)
        return f->required ? fail(p, PNV_E_MISSING, key, NULL, 0, 0) : TRUE;
    scratch = profile_new(protocol->service);
    return apply(f, value, extra_allowed, scratch, p);
}
