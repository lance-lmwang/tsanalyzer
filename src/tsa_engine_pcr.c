#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"

#define TAG "METROLOGY"

typedef struct {
    tsa_handle_t* h;
    tsa_stream_t stream;
    uint32_t pcr_count_since_regress;
} pcr_ctx_t;

static void pcr_on_ts(void* self, const uint8_t* pkt);

static void* pcr_create(void* h, void* context_buf) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)context_buf;
    memset(ctx, 0, sizeof(pcr_ctx_t));
    ctx->h = (tsa_handle_t*)h;

    /* Initialize all possible clock domains with invalid PCR sentinel */
    for (int i = 0; i < TS_PID_MAX; i++) {
        ctx->h->clock_inspectors[i].br_est.last_pcr_ticks = (uint64_t)-1;
    }

    tsa_stream_init(&ctx->stream, ctx, pcr_on_ts);
    return ctx;
}

static void pcr_destroy(void* engine) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)engine;
    tsa_stream_destroy(&ctx->stream);
}

static tsa_stream_t* pcr_get_stream(void* engine) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)engine;
    return &ctx->stream;
}

static void pcr_on_ts(void* self, const uint8_t* pkt) {
    pcr_ctx_t* ctx = (pcr_ctx_t*)self;
    tsa_handle_t* h = ctx->h;
    const ts_decode_result_t* res = &h->current_res;
    uint64_t now = h->current_ns;
    uint16_t pid = res->pid;

    if ((pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x10)) {
        h->live->pid_is_referenced[pid] = true;
        tsa_update_pid_tracker(h, pid);

        /* 🕰️ Capture baseline BEFORE update for accurate drift measurement */
        uint64_t last_local_ns = h->clock_inspectors[pid].last_pcr_local_ns;

        // Update the ClockInspector (handles jitter calculation)
        tsa_clock_update(pkt, &h->clock_inspectors[pid], now, h->config.is_live);

        h->live->pcr_jitter_max_ns = (uint64_t)(fabs(h->clock_inspectors[pid].pcr_jitter_ms) * 1000000.0);
        h->live->pcr_repetition_max_ms = h->clock_inspectors[pid].pcr_interval_max_ticks / (PCR_TICKS_PER_MS);
        h->live->pcr_repetition_error.count = h->clock_inspectors[pid].priority_1_errors;

        uint64_t pt = extract_pcr(pkt);
        uint64_t pcr_ns = pt * 1000 / 27;

        /* Layer 3: Synchronized anchor-based metrology (Per-PID context) */
        uint64_t pid_pkts_now = h->live->pid_packet_count[pid];
        uint64_t pkts_in_interval = (pid_pkts_now >= h->clock_inspectors[pid].br_est.last_total_pkts_anchor) ?
                                    (pid_pkts_now - h->clock_inspectors[pid].br_est.last_total_pkts_anchor) : 0;

        // Piecewise bitrate analysis
        if (h->clock_inspectors[pid].br_est.sync_done && h->clock_inspectors[pid].br_est.last_bitrate_bps > 0) {
            uint64_t bits = pkts_in_interval * TS_PACKET_BITS;
            uint64_t expected = h->clock_inspectors[pid].br_est.last_pcr_ticks + (bits * TS_SYSTEM_CLOCK_HZ / h->clock_inspectors[pid].br_est.last_bitrate_bps);
            int64_t diff = (int64_t)pt - (int64_t)expected;
            if (diff < -((int64_t)1 << 41))
                diff += ((int64_t)1 << 42);
            else if (diff > ((int64_t)1 << 41))
                diff -= ((int64_t)1 << 42);
            h->live->pcr_accuracy_ns_piecewise = (double)diff * 1000.0 / 27.0;
        }

        // Real-time Regression Trigger
        ts_pcr_window_add(&h->pcr_window, now, pcr_ns, pkts_in_interval);
        ts_pcr_window_add(&h->pcr_long_window, now, pcr_ns, pkts_in_interval);

        if (!h->stc_locked) {
            h->stc_ns = pcr_ns;
            h->stc_locked = true;
        }

        if (++ctx->pcr_count_since_regress >= 16) {
            double sl = 1.0, ic = 0.0;
            int64_t pa = 0;
            if (ts_pcr_window_regress(&h->pcr_window, &sl, &ic, &pa) == 0) {
                h->stc_slope_q64 = (int128_t)(sl * 18446744073709551616.0);
                h->stc_intercept_q64 = (int128_t)(ic * 18446744073709551616.0);
                h->live->pcr_accuracy_ns = (double)pa;
                double instant_drift = (sl - 1.0) * 1000000.0;
                if (instant_drift > 1000000.0)
                    instant_drift = 1000000.0;
                else if (instant_drift < -1000000.0)
                    instant_drift = -1000000.0;
                h->live->pcr_drift_ppm = (h->live->pcr_drift_ppm * 0.9) + (instant_drift * 0.1);
                h->stc_wall_drift_ppm = h->live->pcr_drift_ppm;
            }
            ctx->pcr_count_since_regress = 0;
        }

        /* Constant Bitrate Calculation (libeasyice style) */
        uint64_t current_cc_errors = h->live->cc_loss_count + h->live->cc_duplicate_count;
        bool seq_break = (h->clock_inspectors[pid].br_est.sync_done && pt < h->clock_inspectors[pid].br_est.last_pcr_ticks);

        /* 🛡️ Integrity Check: Discard window if any packet loss occurred since last PCR */
        bool integrity_lost = (h->clock_inspectors[pid].br_est.last_cc_count != current_cc_errors);

        if (h->clock_inspectors[pid].br_est.sync_done && !seq_break && !integrity_lost) {
            uint64_t dt_ticks = pt - h->clock_inspectors[pid].br_est.last_pcr_ticks;
            if (dt_ticks > 0) {
                uint64_t bits = pkts_in_interval * TS_PACKET_BITS;
                uint64_t inst_bps = (uint64_t)(((unsigned __int128)bits * 27000000ULL) / dt_ticks);

                /* Store result per PID to support MPTS */
                h->live->pid_bitrate_bps[pid] = inst_bps;
                h->clock_inspectors[pid].br_est.last_bitrate_bps = inst_bps;

                /* 📈 Calculate precise Ticks Per Packet (Q16.16) for pacing analysis */
                if (pkts_in_interval > 0) {
                    h->clock_inspectors[pid].br_est.ticks_per_packet_q16 = (dt_ticks << 16) / pkts_in_interval;
                }

                /* 🕰️ Calculate Real-time PPM Drift (relative to local wall clock) */
                if (h->config.is_live) {
                    uint64_t wall_dt_ns = (now > last_local_ns && last_local_ns > 0) ? (now - last_local_ns) : 0;
                    if (wall_dt_ns > 0) {
                        double pcr_duration_ns = (double)dt_ticks * 1000.0 / 27.0;
                        double instant_drift_ppm = ((pcr_duration_ns - (double)wall_dt_ns) / (double)wall_dt_ns) * 1000000.0;

                        /* Apply smoothing EMA (10% instant, 90% history) */
                        h->clock_inspectors[pid].br_est.pcr_drift_ppm =
                            (h->clock_inspectors[pid].br_est.pcr_drift_ppm * 0.9) + (instant_drift_ppm * 0.1);

                        /* Export to live stats for this PID */
                        h->live->pcr_drift_ppm = h->clock_inspectors[pid].br_est.pcr_drift_ppm;
                    }
                } else {
                    h->clock_inspectors[pid].br_est.pcr_drift_ppm = 0.0;
                    h->live->pcr_drift_ppm = 0.0;
                }

                tsa_debug(TAG, "PCR Bitrate Settlement [PID 0x%04x]: pkts=%lu, dt_ticks=%lu, bitrate=%lu bps, tpp=%.2f, drift=%.1f ppm",
                          pid, (unsigned long)pkts_in_interval, (unsigned long)dt_ticks, (unsigned long)inst_bps,
                          (double)h->clock_inspectors[pid].br_est.ticks_per_packet_q16 / 65536.0,
                          h->clock_inspectors[pid].br_est.pcr_drift_ppm);                /* 🔄 Immediate MPTS Aggregation: Update global rate now
                 * Note: We only sum bitrates from PIDs seen in the last 5 seconds to avoid 'zombie' values. */
                uint64_t total_br = 0;
                for (int i = 0; i < TS_PID_MAX; i++) {
                    if (h->live->pid_bitrate_bps[i] > 0) {
                        if (now > h->live->pid_last_seen_ns[i] && (now - h->live->pid_last_seen_ns[i]) < 5000000000ULL) {
                            total_br += h->live->pid_bitrate_bps[i];
                        } else if (now <= h->live->pid_last_seen_ns[i]) {
                            /* Time might be stationary in tests */
                            total_br += h->live->pid_bitrate_bps[i];
                        }
                    }
                }
                h->live->pcr_bitrate_bps = total_br;

                h->clock_inspectors[pid].br_est.last_pcr_ticks = pt;
                h->clock_inspectors[pid].br_est.last_total_pkts_anchor = pid_pkts_now;
                h->clock_inspectors[pid].br_est.last_cc_count = current_cc_errors;
            }
        } else {
            /* Initial sync, sequence break, or CC error: Reset baseline to ensure next window is accurate */
            if (integrity_lost && !seq_break) {
                tsa_warn(TAG, "PID 0x%04x: Metrology window discarded due to CC error (Pkts lost: %lu)",
                         pid, current_cc_errors - h->clock_inspectors[pid].br_est.last_cc_count);
            }
            h->clock_inspectors[pid].br_est.last_pcr_ticks = pt;
            h->clock_inspectors[pid].br_est.last_total_pkts_anchor = pid_pkts_now;
            h->clock_inspectors[pid].br_est.last_cc_count = current_cc_errors;
            h->clock_inspectors[pid].br_est.sync_done = true;
            h->live->pid_bitrate_bps[pid] = h->clock_inspectors[pid].br_est.last_bitrate_bps;
        }
    }
}

tsa_plugin_ops_t pcr_ops = {
    .name = "PCR_ANALYSIS",
    .create = pcr_create,
    .destroy = pcr_destroy,
    .get_stream = pcr_get_stream,
};

void tsa_register_pcr_engine(tsa_handle_t* h) {
    extern void tsa_plugin_attach_instance(tsa_handle_t * h, tsa_plugin_ops_t * ops);
    tsa_plugin_attach_instance(h, &pcr_ops);
}
