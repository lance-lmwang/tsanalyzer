#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "tsa.h"
#include "tsa_internal.h"

/**
 * @brief High-Precision Clock Drift Analysis
 * 
 * Uses the phase difference between the window's first and last samples 
 * to calculate the average frequency ratio (slope). This method is 
 * robust against intermediate OS scheduling jitter.
 */
int ts_pcr_window_regress(ts_pcr_window_t* w, double* out_slope, double* out_intercept, int64_t* out_max_err) {
    if (!w || w->count < 10) return -1;
    
    uint32_t n = w->count;
    uint32_t head_idx = (w->head - 1 + w->size) % w->size;
    uint32_t tail_idx = (w->head - n + w->size) % w->size;
    
    ts_pcr_sample_t first = w->samples[tail_idx];
    ts_pcr_sample_t last = w->samples[head_idx];
    
    uint64_t d_sys = last.sys_ns - first.sys_ns;
    uint64_t d_pcr = last.pcr_ns - first.pcr_ns;
    
    if (d_sys == 0) return -1;
    
    /* Calculate average slope (frequency ratio) over the window */
    double slope = (double)d_pcr / (double)d_sys;
    
    /* Use first sample as intercept origin for stability */
    double intercept = 0.0; 
    
    if (out_slope) *out_slope = slope;
    if (out_intercept) *out_intercept = (double)first.pcr_ns;
    
    /* Accuracy Check: Maximum jitter relative to the average trend */
    double max_err = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t idx = (w->head - n + k + w->size) % w->size;
        double x = (double)(w->samples[idx].sys_ns - first.sys_ns);
        double y = (double)(w->samples[idx].pcr_ns - first.pcr_ns);
        double err = fabs(y - (slope * x));
        if (err > max_err) max_err = err;
    }
    if (out_max_err) *out_max_err = (int64_t)max_err;
    
    return 0;
}
