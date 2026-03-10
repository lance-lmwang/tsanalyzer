#include <math.h>

#include "tsa_internal.h"

float tsa_calculate_health(tsa_handle_t* h) {
    if (!h || !h->live) return 0.0f;

    float score = 100.0f;

    // P1 Errors - Critical (Deduct 25-50)
    if (h->alerts[TSA_ALERT_SYNC].status == TSA_ALERT_STATE_FIRING) {
        score -= 50.0f;
    }
    if (h->alerts[TSA_ALERT_PAT].status == TSA_ALERT_STATE_FIRING) {
        score -= 40.0f;
    }
    if (h->alerts[TSA_ALERT_CC].status == TSA_ALERT_STATE_FIRING) {
        score -= 25.0f;
    }
    if (h->alerts[TSA_ALERT_PMT].status == TSA_ALERT_STATE_FIRING) {
        score -= 30.0f;
    }

    // P2 Errors - Major (Deduct 10-20)
    if (h->alerts[TSA_ALERT_TRANSPORT].status == TSA_ALERT_STATE_FIRING) {
        score -= 20.0f;
    }
    if (h->alerts[TSA_ALERT_PCR].status == TSA_ALERT_STATE_FIRING) {
        score -= 10.0f;
    }
    if (h->alerts[TSA_ALERT_PTS].status == TSA_ALERT_STATE_FIRING) {
        score -= 15.0f;
    }
    if (h->alerts[TSA_ALERT_CRC].status == TSA_ALERT_STATE_FIRING) {
        score -= 5.0f;
    }

    // P3 Errors - Minor (Deduct 5)
    if (h->alerts[TSA_ALERT_SDT].status == TSA_ALERT_STATE_FIRING) {
        score -= 5.0f;
    }
    if (h->alerts[TSA_ALERT_NIT].status == TSA_ALERT_STATE_FIRING) {
        score -= 5.0f;
    }

    // Predictive/Metrology Deductions
    double jitter_ms = (double)h->live->pcr_jitter_max_ns / 1000000.0;
    if (jitter_ms > 30.0) {
        score -= (float)((jitter_ms - 30.0) * 0.5);
    }

    if (score < 0.0f) score = 0.0f;
    if (score > 100.0f) score = 100.0f;

    return score;
}
