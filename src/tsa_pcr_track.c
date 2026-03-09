#include "tsa_pcr_track.h"

#include <math.h>
#include <string.h>

#include "tsa_bitstream.h"
#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "PCR_TRACK"
#define PCR_REPETITION_MAX_TICKS (TS_SYSTEM_CLOCK_HZ * 40 / 1000) /* 40ms */
#define PCR_ARRIVAL_MAX_NS 100000000ULL                           /* 100ms arrival limit */
#define BITRATE_SETTLE_PKTS 100
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
    memset(track, 0, sizeof(tsa_pcr_track_t));
    track->pid = pid;
    track->program_id = prg_id;
}

static int pcr_track_regress(tsa_pcr_track_t* track, double* out_slope, double* out_intercept, int64_t* out_max_err) {
    uint32_t n = track->clock.count;
    if (n < 10) return -1;

    double avg_x = 0, avg_y = 0;
    for (uint32_t k = 0; k < n; k++) {
        avg_x += (double)track->clock.samples[k].sys_ns / 1e9;
        avg_y += (double)track->clock.samples[k].pcr_ns / 1e9;
    }
    avg_x /= n;
    avg_y /= n;

    double num = 0, den = 0;
    for (uint32_t k = 0; k < n; k++) {
        double dx = ((double)track->clock.samples[k].sys_ns / 1e9) - avg_x;
        double dy = ((double)track->clock.samples[k].pcr_ns / 1e9) - avg_y;
        num += dx * dy;
        den += dx * dx;
    }

    if (fabs(den) < 1e-12) return -1;
    double slope = num / den;
    double intercept = avg_y - (slope * avg_x);

    if (out_slope) *out_slope = slope;
    if (out_intercept) *out_intercept = intercept * 1e9;

    double max_err_ns = 0;
    for (uint32_t k = 0; k < n; k++) {
        double px = (double)track->clock.samples[k].sys_ns / 1e9;
        double py = (double)track->clock.samples[k].pcr_ns / 1e9;
        double err = fabs(py - (slope * px + intercept));
        if (err > max_err_ns) max_err_ns = err;
    }
    if (out_max_err) *out_max_err = (int64_t)(max_err_ns * 1e9);
    return 0;
}

bool tsa_pcr_track_update(tsa_pcr_track_t* track, uint64_t pcr_ticks, uint64_t arrival_ns, uint64_t pid_pkts,
                          uint64_t total_pkts, bool use_arrival_check) {
    if (!track) return false;

    if (!track->initialized) {
        track->last_pcr_ticks = pcr_ticks;
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

    uint64_t pcr_delta = (pcr_ticks >= track->last_pcr_ticks) ? (pcr_ticks - track->last_pcr_ticks)
                                                              : (pcr_ticks + PCR_MODULO - track->last_pcr_ticks);
    uint64_t arrival_delta_ns = (arrival_ns > track->last_arrival_ns) ? (arrival_ns - track->last_arrival_ns) : 0;

    bool arrival_violation = (use_arrival_check && arrival_delta_ns > PCR_ARRIVAL_MAX_NS);
    bool pcr_gap_violation =
        (pcr_ticks != INVALID_PCR && track->last_pcr_ticks != INVALID_PCR && pcr_delta > PCR_REPETITION_MAX_TICKS);

    if (pcr_gap_violation || arrival_violation) {
        track->priority_1_errors++;
        tsa_warn(TAG, "PID 0x%04x: PCR Repetition Violation (PCR Gap: %.2f ms, Arrival Gap: %.2f ms)", track->pid,
                 (pcr_ticks != INVALID_PCR) ? tsa_pcr_to_ns_f((double)pcr_delta) / 1e6 : 0.0,
                 (double)arrival_delta_ns / 1e6);
        if (pcr_ticks != INVALID_PCR && pcr_delta > (TS_SYSTEM_CLOCK_HZ * 5)) {
            tsa_pcr_track_reset(track);
            return false;
        }
        if (arrival_violation) track->clock.locked = false;
    }

    if (pcr_ticks != INVALID_PCR) {
        uint64_t pcr_ns = tsa_pcr_to_ns(pcr_ticks);
        uint32_t head = track->clock.head;
        track->clock.samples[head].sys_ns = arrival_ns;
        track->clock.samples[head].pcr_ns = pcr_ns;
        track->clock.head = (head + 1) % PCR_TRACK_WINDOW_SIZE;
        if (track->clock.count < PCR_TRACK_WINDOW_SIZE) track->clock.count++;

        if (track->clock.count >= 10) {
            double slope, intercept;
            int64_t max_err_ns;
            if (pcr_track_regress(track, &slope, &intercept, &max_err_ns) == 0) {
                track->clock.slope = slope;
                track->clock.intercept = intercept;
                track->clock.locked = (max_err_ns < 1000000);
                track->drift_ppm = (slope - 1.0) * 1e6;
                track->pcr_jitter_ms = (float)(max_err_ns / 1000000.0);
            }
        }
    }

    uint64_t pkts_in_interval = total_pkts - track->anchors.last_total_pkts_anchor;
    if (pkts_in_interval >= BITRATE_SETTLE_PKTS && pcr_ticks != INVALID_PCR) {
        uint64_t dt_ticks = (pcr_ticks >= track->anchors.last_pcr_ticks_anchor)
                                ? (pcr_ticks - track->anchors.last_pcr_ticks_anchor)
                                : (pcr_ticks + PCR_MODULO - track->anchors.last_pcr_ticks_anchor);

        if (dt_ticks > 0) {
            /* Standard ISO/IEC 13818-1 Bitrate Formula:
             * Rate = (ΔPackets * 188 * 8 * 27,000,000) / ΔPCR_ticks
             * Using double to ensure precision during calculation. */
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

    if (pcr_ticks != INVALID_PCR) track->last_pcr_ticks = pcr_ticks;
    track->last_arrival_ns = arrival_ns;
    track->pcr_count++;
    return true;
}
