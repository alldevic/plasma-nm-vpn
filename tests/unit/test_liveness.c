/* SPDX-License-Identifier: MIT */
/* The liveness scheduling rules (5.4): the failure counter and the AmneziaWG handshake-age threshold. */
#include "pnv-daemon.h"

static void test_counter(void)
{
    static const struct {
        const char *results; /* + passed, - failed */
        PnvLivenessAction last;
    } cases[] = {
        {"+", PNV_LIVENESS_OK},   {"-", PNV_LIVENESS_FAILED},     {"--", PNV_LIVENESS_FAILED},
        {"---", PNV_LIVENESS_DEAD}, {"--+--", PNV_LIVENESS_FAILED}, {"--+---", PNV_LIVENESS_DEAD},
        {"-+-+-+", PNV_LIVENESS_OK},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        guint failures = 0;
        PnvLivenessAction action = PNV_LIVENESS_OK;
        for (const char *r = cases[i].results; *r; r++)
            action = pnv_liveness_record(&failures, *r == '+');
        if (action != cases[i].last)
            g_error("\"%s\": got %d, want %d", cases[i].results, action, cases[i].last);
    }
}

/* A fake clock: a server that dies at t=0 after a handshake at t=0, checks every 60 s from an arbitrary phase. */
static guint detection(guint rekey, guint phase)
{
    guint threshold = pnv_awg_age_threshold(rekey), failures = 0;

    for (guint t = phase;; t += PNV_LIVENESS_INTERVAL)
        if (pnv_liveness_record(&failures, t <= threshold) == PNV_LIVENESS_DEAD)
            return t;
}

static void test_threshold(void)
{
    g_assert_cmpuint(pnv_awg_age_threshold(0), ==, 190);
    g_assert_cmpuint(pnv_awg_age_threshold(120), ==, 190);
    g_assert_cmpuint(pnv_awg_age_threshold(60), ==, 190);
    g_assert_cmpuint(pnv_awg_age_threshold(300), ==, 370);
    /* Above the responder's worst case without a rekey (RejectAfterTime, 180 s). */
    g_assert_cmpuint(pnv_awg_age_threshold(120), >, 180);
    for (guint phase = 0; phase < PNV_LIVENESS_INTERVAL; phase += 7) {
        guint t = detection(120, phase);
        g_assert_cmpuint(t, <=, pnv_awg_detection_bound(120));
        g_assert_cmpuint(t, >, pnv_awg_age_threshold(120));
    }
    g_assert_cmpuint(pnv_awg_detection_bound(120), ==, 370);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/liveness/counter", test_counter);
    g_test_add_func("/liveness/threshold", test_threshold);
    return g_test_run();
}
