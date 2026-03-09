#include "tsa_pcr_track.h"

#include <math.h>
#include <string.h>

#include "tsa_bitstream.h"
#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "PCR_TRACK"
#define PCR_REPETITION_MAX_TICKS (TS_SYSTEM_CLOCK_HZ * 40 / 1000) /* 40ms per TR 101 290 */
#define PCR_ARRIVAL_MAX_NS 100000000ULL                           /* 100ms arrival limit */
#define BITRATE_SETTLE_INTERVAL_NS 100000000ULL                   /* 100ms minimum window */
#define PCR_BASE_CLOCK_HZ (TS_SYSTEM_CLOCK_HZ / 300)
#define PCR_MODULO ((1ULL << 33) * 300)
#define PCR_BITRATE_EMA_ALPHA 0.1

void tsa_pcr_track_init(tsa_pcr_track_t* track, uint32_t pid, uint32_t prg_id) {
    if (!track) return;
    memset(track, 0, sizeof(tsa_pcr_track_t));
    track->pid = pid;
    track->program_id = prg_id;
}

void tsa_pcr_track_destroy(tsa_pcr_track_t* track) {
    (void)track;
}

void tsa_pcr_track_reset(tsa_pcr_track_t* track) {
    if (!track) return;
    uint32_t pid = track->pid;
    uint32_t prg_id = track->program_id;
    uint32_t p1_errors = track->priority_1_errors;
    memset(track, 0, sizeof(tsa_pcr_track_t));
    track->pid = pid;
    track->program_id = prg_id;
    track->priority_1_errors = p1_errors; /* Maintain error counters */
}

/* Internal helper: Perform Linear Regression on the inline window.
 * Mathematically stable implementation: subtracts baseline to prevent precision collapse. */
static int pcr_track_regress(tsa_pcr_track_t* track, double* out_slope, double* out_intercept, int64_t* out_max_err) {
    uint32_t n = track->clock.count;
    if (n < 10) return -1;

    /* Base offsets to bring values into small range (~seconds) instead of ~10^18 ns */
    uint64_t t0_sys = track->clock.samples[0].sys_ns;
    uint64_t t0_pcr = track->clock.samples[0].pcr_ns;

    double sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
    for (uint32_t k = 0; k < n; k++) {
        double x = (double)(track->clock.samples[k].sys_ns - t0_sys) / 1e9;
        double y = (double)(track->clock.samples[k].pcr_ns - t0_pcr) / 1e9;
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }

    double den = (n * sum_xx - sum_x * sum_x);
    if (fabs(den) < 1e-12) return -1;

    double slope = (n * sum_xy - sum_x * sum_y) / den;
    /* Intercept in the relative coordinate system (offset from t0) */
    double rel_intercept_s = (sum_y - slope * sum_x) / n;

    /* Convert back to absolute intercept for the model: pcr_ns = slope * sys_ns + intercept */
    double abs_intercept = (double)t0_pcr - (slope * (double)t0_sys) + (rel_intercept_s * 1e9);

    if (out_slope) *out_slope = slope;
    if (out_intercept) *out_intercept = abs_intercept;

    /* Calculate Max Error (Jitter) in absolute nanoseconds */
    double max_err_ns = 0;
    for (uint32_t k = 0; k < n; k++) {
        double px = (double)track->clock.samples[k].sys_ns;
        double py = (double)track->clock.samples[k].pcr_ns;
        double err = fabs(py - (slope * px + abs_intercept));
        if (err > max_err_ns) max_err_ns = err;
    }
    if (out_max_err) *out_max_err = (int64_t)max_err_ns;

    return 0;
}

bool tsa_pcr_track_update(tsa_pcr_track_t* track, uint64_t pcr_ticks, uint64_t arrival_ns, uint64_t pid_pkts,
                          uint64_t total_pkts, bool use_arrival_check) {
    if (!track) return false;

    if (!track->initialized) {
        if (pcr_ticks == INVALID_PCR) return false; /* Need a valid PCR to start */
        track->last_pcr_ticks = pcr_ticks;
        track->last_unwrapped_pcr_ns = tsa_pcr_to_ns(pcr_ticks);
        track->last_arrival_ns = arrival_ns;
        track->pcr_count = 1;
        track->initialized = true;

        track->anchors.last_pcr_ticks_anchor = pcr_ticks;
        track->anchors.last_arrival_ns_anchor = arrival_ns;
        track->anchors.last_pid_pkts_anchor = pid_pkts;
        track->anchors.last_total_pkts_anchor = total_pkts;
        track->anchors.sync_done = false;
        return false;
    }

    /* 1. Gap Analysis */
    uint64_t arrival_delta_ns = (arrival_ns > track->last_arrival_ns) ? (arrival_ns - track->last_arrival_ns) : 0;
    bool arrival_violation = (use_arrival_check && arrival_delta_ns > PCR_ARRIVAL_MAX_NS);
    bool pcr_gap_violation = false;
    uint64_t pcr_delta_ticks = 0;

    if (pcr_ticks != INVALID_PCR) {
        pcr_delta_ticks = (pcr_ticks >= track->last_pcr_ticks) ? (pcr_ticks - track->last_pcr_ticks)
                                                               : (pcr_ticks + PCR_MODULO - track->last_pcr_ticks);

        /* Update Monotonic PCR Time Axis (Unwrapping) only on valid PCR packets */
        track->last_unwrapped_pcr_ns += tsa_pcr_to_ns(pcr_delta_ticks);
        pcr_gap_violation = (pcr_delta_ticks > PCR_REPETITION_MAX_TICKS);
    }

    if (pcr_gap_violation || arrival_violation) {
        track->priority_1_errors++;
        tsa_warn(TAG, "PID 0x%04x: PCR Repetition Violation (PCR Gap: %.2f ms, Arrival Gap: %.2f ms)", track->pid,
                 (pcr_ticks != INVALID_PCR) ? tsa_pcr_to_ns_f((double)pcr_delta_ticks) / 1e6 : 0.0,
                 (double)arrival_delta_ns / 1e6);

        /* Reset on large time jumps (>5s) */
        if (pcr_ticks != INVALID_PCR && pcr_delta_ticks > (TS_SYSTEM_CLOCK_HZ * 5)) {
            tsa_pcr_track_reset(track);
            return false;
        }
        if (arrival_violation) track->clock.locked = false;
    }

    /* 2. Clock Recovery (LRM) - Only on valid PCR */
    if (pcr_ticks != INVALID_PCR) {
        uint32_t head = track->clock.head;
        track->clock.samples[head].sys_ns = arrival_ns;
        track->clock.samples[head].pcr_ns = track->last_unwrapped_pcr_ns;
        track->clock.head = (head + 1) % PCR_TRACK_WINDOW_SIZE;
        if (track->clock.count < PCR_TRACK_WINDOW_SIZE) track->clock.count++;

        if (track->clock.count >= 10) {
            double slope, intercept;
            int64_t max_err_ns;
            if (pcr_track_regress(track, &slope, &intercept, &max_err_ns) == 0) {
                track->clock.slope = slope;
                track->clock.intercept = intercept;
                track->clock.locked = (max_err_ns < 1000000); /* 1ms lock threshold */
                track->drift_ppm = (slope - 1.0) * 1e6;

                /* EMA Smoothing for Jitter */
                float inst_jitter_ms = (float)(max_err_ns / 1000000.0);
                if (track->pcr_jitter_ms == 0)
                    track->pcr_jitter_ms = inst_jitter_ms;
                else
                    track->pcr_jitter_ms = (0.1f * inst_jitter_ms) + (0.9f * track->pcr_jitter_ms);
            }
        }
    }

    /* 3. Bitrate Settlement (Anchor-based, minimum 100ms window) */
    uint64_t time_since_last_settle =
        (arrival_ns > track->anchors.last_arrival_ns_anchor) ? (arrival_ns - track->anchors.last_arrival_ns_anchor) : 0;

    if (time_since_last_settle >= BITRATE_SETTLE_INTERVAL_NS && pcr_ticks != INVALID_PCR) {
        uint64_t dt_ticks = (pcr_ticks >= track->anchors.last_pcr_ticks_anchor)
                                ? (pcr_ticks - track->anchors.last_pcr_ticks_anchor)
                                : (pcr_ticks + PCR_MODULO - track->anchors.last_pcr_ticks_anchor);

        if (dt_ticks > 0) {
            double dp = (double)(total_pkts - track->anchors.last_total_pkts_anchor);
            double dp_pid = (double)(pid_pkts - track->anchors.last_pid_pkts_anchor);

            uint64_t inst_mux_bitrate =
                (uint64_t)(dp * (double)TS_PACKET_BITS * (double)TS_SYSTEM_CLOCK_HZ / (double)dt_ticks);
            uint64_t inst_bitrate =
                (uint64_t)(dp_pid * (double)TS_PACKET_BITS * (double)TS_SYSTEM_CLOCK_HZ / (double)dt_ticks);

            if (track->bitrate_bps == 0)
                track->bitrate_bps = inst_bitrate;
            else
                track->bitrate_bps = (uint64_t)(PCR_BITRATE_EMA_ALPHA * inst_bitrate +
                                                (1.0 - PCR_BITRATE_EMA_ALPHA) * track->bitrate_bps);

            if (track->mux_bitrate_bps == 0)
                track->mux_bitrate_bps = inst_mux_bitrate;
            else
                track->mux_bitrate_bps = (uint64_t)(PCR_BITRATE_EMA_ALPHA * inst_mux_bitrate +
                                                    (1.0 - PCR_BITRATE_EMA_ALPHA) * track->mux_bitrate_bps);

            track->anchors.last_pcr_ticks_anchor = pcr_ticks;
            track->anchors.last_arrival_ns_anchor = arrival_ns;
            track->anchors.last_pid_pkts_anchor = pid_pkts;
            track->anchors.last_total_pkts_anchor = total_pkts;
            track->anchors.sync_done = true;
        }
    }

    /* Final reference updates */
    if (pcr_ticks != INVALID_PCR) {
        track->last_pcr_ticks = pcr_ticks;
    }
    track->last_arrival_ns = arrival_ns;
    track->pcr_count++;
    return true;
}
