#include <math.h>

#include "tsa_internal.h"

float tsa_calculate_health(tsa_handle_t* h) {
    if (!h || !h->live) return 0.0f;

    float score = 100.0f;

    // P1 Errors - Critical (Deduct 25-50)
    if (!h->signal_lock) {
        score -= 50.0f;
    }
    if (h->live->alarm_pat_error) {
        score -= 40.0f;
    }
    if (h->debounce_cc.is_fired) {
        score -= 25.0f;
    }
    if (h->live->alarm_pmt_error) {
        score -= 30.0f;
    }

    // P2 Errors - Major (Deduct 10-20)
    if (h->debounce_transport.is_fired) {
        score -= 20.0f;
    }
    if (h->live->alarm_pcr_repetition_error) {
        score -= 10.0f;
    }
    if (h->live->alarm_pcr_accuracy_error) {
        score -= 10.0f;
    }
    if (h->debounce_pts.is_fired) {
        score -= 15.0f;
    }
    if (h->live->alarm_crc_error) {
        score -= 5.0f;
    }

    // P3 Errors - Minor (Deduct 5)
    if (h->live->sdt_timeout.count > h->prev_snap_base->sdt_timeout.count) {
        score -= 5.0f;
    }
    if (h->live->nit_timeout.count > h->prev_snap_base->nit_timeout.count) {
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
