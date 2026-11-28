/* SPDX-License-Identifier: MIT */
/* The import parsers (6.6), one target per format (FUZZ_PREFIX): the input follows the format's opening, so the
 * fuzzer spends its time inside the parser. Built three times by CMake. */
#include <string.h>

#include "pnv-profile.h"

int LLVMFuzzerTestOneInput(const guint8 *bytes, size_t size);

/* Small lists in the shape of the cores' ones, so allowlisted parameters are reachable. */
static const char *const lists[][2] = {
    {"extra/", "udp_fragment\nnetwork\ntls\ntls.min_version\n"},
    {"xhttp-extra/", "headers\nheaders.*\nxPaddingBytes\n"},
    {"tls-fingerprint/", "chrome\nfirefox\n"},
    {"xhttp-mode/", "auto\npacket-up\n"},
    {"awg/", "awg-jc Jc uint16\nawg-h1 H1 u32range\nawg-i1 I1 chain\nawg-randomtrailers RandomTrailers onoff\n"},
};

static void check(const char *value)
{
    g_assert(!strchr(value, '\n') && g_utf8_validate(value, -1, NULL));
}

int LLVMFuzzerTestOneInput(const guint8 *bytes, size_t size)
{
    g_autoptr(PnvList) all = pnv_list_new();
    g_autofree char *text = g_malloc(strlen(FUZZ_PREFIX) + size + 1);
    g_auto(PnvProblem) problem = {0};
    g_autoptr(PnvImport) import = NULL;
    GHashTableIter it;
    gpointer key, value;

    for (guint i = 0; i < G_N_ELEMENTS(lists); i++) {
        g_autoptr(PnvList) one = pnv_list_parse(lists[i][1], "fuzz", NULL);
        pnv_list_merge(all, one, lists[i][0]);
    }
    memcpy(text, FUZZ_PREFIX, strlen(FUZZ_PREFIX));
    memcpy(text + strlen(FUZZ_PREFIX), bytes, size);
    text[strlen(FUZZ_PREFIX) + size] = '\0';
    import = pnv_import(text, all, &problem);
    if (!import) {
        g_autofree char *reason = pnv_problem_describe(&problem);
        g_assert(!strchr(reason, '\n'));
        g_assert(problem.code >= PNV_CODES_VALIDATOR_FIRST && problem.code <= PNV_CODES_PARSER_LAST);
        return 0;
    }
    /* What comes out passed the validators: no line breaks, valid UTF-8. */
    g_hash_table_iter_init(&it, import->data);
    while (g_hash_table_iter_next(&it, &key, &value))
        check(value);
    g_hash_table_iter_init(&it, import->secrets);
    while (g_hash_table_iter_next(&it, &key, &value))
        check(value);
    for (guint i = 0; i < import->warnings->len; i++) {
        PnvCode code = g_array_index(import->warnings, PnvProblem, i).code;
        g_assert(code >= PNV_CODES_PARSER_FIRST && code <= PNV_CODES_PARSER_LAST);
    }
    return 0;
}
