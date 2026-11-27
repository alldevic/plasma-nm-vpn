/* SPDX-License-Identifier: MIT */
/* extra: a JSON object merged into the core's outbound. Every member's path must be on the allowlist for the
 * core version; detour and file paths are refused even if a list names them. */
#include <string.h>

#include "pnv-profile.h"

#define EXTRA_MAX_SIZE 8192
#define EXTRA_MAX_DEPTH 8

static gboolean extra_fail(PnvProblem *p, PnvCode code, const char *key, const char *path, gint64 max)
{
    if (p) {
        pnv_problem_clear(p);
        *p = (PnvProblem){code, g_strdup(key), g_strdup(path), -1, 0, max};
    }
    return FALSE;
}

static gboolean forbidden(const char *name)
{
    return strcmp(name, "detour") == 0 || strcmp(name, "path") == 0 || g_str_has_suffix(name, "_path");
}

static gboolean check_node(const char *key, JsonNode *node, const char *path, const PnvList *allowed, PnvProblem *p);

static gboolean check_object(const char *key, JsonObject *object, const char *path, const PnvList *allowed,
                             PnvProblem *p)
{
    JsonObjectIter iter;
    const char *name;
    JsonNode *member;

    json_object_iter_init(&iter, object);
    while (json_object_iter_next(&iter, &name, &member)) {
        g_autofree char *member_path = path ? g_strconcat(path, ".", name, NULL) : g_strdup(name);

        if (forbidden(name))
            return extra_fail(p, PNV_E_EXTRA_FORBIDDEN, key, member_path, 0);
        if (!pnv_list_contains(allowed, member_path))
            return extra_fail(p, PNV_E_EXTRA_NOT_ALLOWED, key, member_path, 0);
        if (!check_node(key, member, member_path, allowed, p))
            return FALSE;
    }
    return TRUE;
}

/* Members are checked recursively; arrays hold only scalars (or arrays of them); strings follow the common
 * rule after unescaping, so "\u000a" is a newline as well. */
static gboolean check_node(const char *key, JsonNode *node, const char *path, const PnvList *allowed, PnvProblem *p)
{
    switch (JSON_NODE_TYPE(node)) {
    case JSON_NODE_OBJECT:
        return check_object(key, json_node_get_object(node), path, allowed, p);
    case JSON_NODE_ARRAY: {
        g_autoptr(GList) elements = json_array_get_elements(json_node_get_array(node));
        for (GList *e = elements; e; e = e->next) {
            if (JSON_NODE_HOLDS_OBJECT(e->data))
                return extra_fail(p, PNV_E_EXTRA_BAD_VALUE, key, path, 0);
            if (!check_node(key, e->data, path, allowed, p))
                return FALSE;
        }
        return TRUE;
    }
    case JSON_NODE_VALUE:
        if (json_node_get_value_type(node) == G_TYPE_STRING &&
            !pnv_check_string(key, json_node_get_string(node), EXTRA_MAX_SIZE, NULL))
            return extra_fail(p, PNV_E_EXTRA_BAD_VALUE, key, path, 0);
        return TRUE;
    case JSON_NODE_NULL:
        break;
    }
    return extra_fail(p, PNV_E_EXTRA_BAD_VALUE, key, path, 0);
}

/* The deepest nesting of objects and arrays, counted on the text: json-glib recurses while parsing. */
static gint nesting(const char *text)
{
    gint depth = 0, max = 0;
    gboolean in_string = FALSE;

    for (const char *c = text; *c; c++) {
        if (in_string) {
            if (*c == '\\' && c[1])
                c++;
            else if (*c == '"')
                in_string = FALSE;
        } else if (*c == '"') {
            in_string = TRUE;
        } else if (*c == '{' || *c == '[') {
            max = MAX(max, ++depth);
        } else if (*c == '}' || *c == ']') {
            depth--;
        }
    }
    return max;
}

gboolean pnv_parse_extra(const char *key, const char *value, const PnvList *allowed, JsonObject **out,
                         PnvProblem *p)
{
    g_autoptr(JsonParser) parser = NULL;
    JsonNode *root;

    if (!pnv_check_string(key, value, EXTRA_MAX_SIZE, p))
        return FALSE;
    if (nesting(value) > EXTRA_MAX_DEPTH)
        return extra_fail(p, PNV_E_JSON_TOO_DEEP, key, NULL, EXTRA_MAX_DEPTH);
    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, value, -1, NULL))
        return extra_fail(p, PNV_E_BAD_JSON, key, NULL, 0);
    root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root))
        return extra_fail(p, PNV_E_JSON_NOT_OBJECT, key, NULL, 0);
    if (!allowed)
        return extra_fail(p, PNV_E_EXTRA_NOT_ALLOWED, key, NULL, 0);
    if (!check_object(key, json_node_get_object(root), NULL, allowed, p))
        return FALSE;
    if (out)
        *out = json_object_ref(json_node_get_object(root));
    return TRUE;
}
