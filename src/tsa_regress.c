#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "tsa_internal.h"

int ts_pcr_window_regress(ts_pcr_window_t* w, double* out_slope, double* out_intercept, int64_t* out_max_err) {
    if (!w || w->count < 10) return -1;

    uint32_t n = w->count;
    double avg_x = 0, avg_y = 0;
    
    /* 1. Scale to Seconds to avoid float64 precision collapse with large ns values */
    for (uint32_t k = 0; k < n; k++) {
        uint32_t idx = (w->head - n + k + w->size) % w->size;
        avg_x += (double)w->samples[idx].sys_ns / 1e9;
        avg_y += (double)w->samples[idx].pcr_ns / 1e9;
    }
    avg_x /= n;
    avg_y /= n;

    double num = 0, den = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t idx = (w->head - n + k + w->size) % w->size;
        double dx = ((double)w->samples[idx].sys_ns / 1e9) - avg_x;
        double dy = ((double)w->samples[idx].pcr_ns / 1e9) - avg_y;
        num += dx * dy;
        den += dx * dx;
    }

    if (fabs(den) < 1e-12) return -1;

    double slope = num / den;
    double intercept = avg_y - (slope * avg_x);

    if (out_slope) *out_slope = slope;
    if (out_intercept) *out_intercept = intercept * 1e9; /* Back to ns */

    double max_err_ns = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t idx = (w->head - n + k + w->size) % w->size;
        double px = (double)w->samples[idx].sys_ns / 1e9;
        double py = (double)w->samples[idx].pcr_ns / 1e9;
        double err = fabs(py - (slope * px + intercept));
        if (err > max_err_ns) max_err_ns = err;
    }
    if (out_max_err) *out_max_err = (int64_t)(max_err_ns * 1e9);

    return 0;
}
