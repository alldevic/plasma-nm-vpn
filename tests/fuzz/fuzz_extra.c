/* SPDX-License-Identifier: MIT */
/* extra: JSON text → allowed object or problem. */
#include "pnv-profile.h"

int LLVMFuzzerTestOneInput(const guint8 *bytes, size_t size);

int LLVMFuzzerTestOneInput(const guint8 *bytes, size_t size)
{
    g_autoptr(PnvList) allowed = pnv_list_parse("up_mbps\ndown_mbps\ntls\ntls.fragment\ntls.alpn\n", "fuzz", NULL);
    g_autofree char *text = g_strndup((const char *)bytes, size);
    g_auto(PnvProblem) problem = {0};
    g_autoptr(JsonObject) extra = NULL;

    if (pnv_parse_extra("extra", text, allowed, &extra, &problem))
        g_assert(extra);
    return 0;
}
