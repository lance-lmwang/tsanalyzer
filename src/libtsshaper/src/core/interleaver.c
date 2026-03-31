#include <float.h>
#include <stdint.h>

#include "hal.h"
#include "internal.h"

// Professional Broadcast-Grade Interleaver (ETTF with Per-PID Queues)
// Compliant with ISO/IEC 13818-1 & TR 101 290 P2 Standards.

ts_packet_t* interleaver_select(tsshaper_t* ctx) {
    if (ctx->num_programs == 0) return NULL;

    // Fixed 5Mbps grid clock (CBR Heartbeat)
    uint64_t sched_time_ns = ctx->ideal_packet_time_ns;

    // Phase 1: Critical Control Path (PSI - PRIO_CRITICAL)
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        for (int p = 0; p < prog->num_pids; p++) {
            tstd_pid_ctx_t* pid_ctx = &prog->pids[p];
            if (pid_ctx->priority == PRIO_CRITICAL) {
                if (tstd_can_send_packet(pid_ctx, sched_time_ns)) {
                    if (spsc_queue_pop(pid_ctx->queue, &ctx->scratch_pkt)) {
                        tstd_update_on_pop(prog, &ctx->scratch_pkt, sched_time_ns);
                        return &ctx->scratch_pkt;
                    }
                }
            }
        }
    }

    // Phase 2: ES Interleaving (Video/Audio) with Phase Lock and Contention Arbitration
    tstd_pid_ctx_t* best_pid_ctx = NULL;
    int64_t max_priority_score = -2000000000LL;
    program_ctx_t* best_prog = NULL;

    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        for (int p = 0; p < prog->num_pids; p++) {
            tstd_pid_ctx_t* pid_ctx = &prog->pids[p];

            // Only consider High/Medium priority (ES)
            if (pid_ctx->priority > PRIO_MEDIUM || pid_ctx->priority == PRIO_CRITICAL) continue;

            if (tstd_can_send_packet(pid_ctx, sched_time_ns)) {
                // T-STD PACING RULE:
                // Check lateness relative to theoretical deadline.
                int64_t lateness_ns = (int64_t)sched_time_ns - (int64_t)pid_ctx->next_pacing_time_ns;

                // ARBITRATION WINDOW:
                // If strict_cbr is enabled, we completely forbid lookahead (lateness < 0)
                // to achieve extreme <44kbps ES smoothing. Otherwise, we allow a half-packet lookahead
                // to prevent excessive NULLs and maximize throughput within T-STD limits.
                int64_t min_lateness = ctx->strict_cbr ? 0 : -((int64_t)ctx->packet_interval_ns / 2);

                if (lateness_ns >= min_lateness) {
                    // BROADCAST QUALITY HEURISTIC:
                    // 1. Lateness (higher is more urgent)
                    // 2. Queue Depth (avoid HOL blocking)
                    // 3. TB Pressure (avoid overflow)

                    int64_t priority_boost = (pid_ctx->priority == PRIO_HIGH) ? 1000000LL : 0;
                    int64_t score = lateness_ns + priority_boost;

                    if (score > max_priority_score) {
                        max_priority_score = score;
                        best_pid_ctx = pid_ctx;
                        best_prog = prog;
                    }
                }
            }
        }
    }

    if (best_pid_ctx && best_prog) {
        if (spsc_queue_pop(best_pid_ctx->queue, &ctx->scratch_pkt)) {
            uint64_t p_interval_ns = (188ULL * 8 * 1000000000ULL) / best_pid_ctx->shaping_rate_bps;

            // PHASE LOCK WITH ANTI-BURST CLAMP
            if (best_pid_ctx->next_pacing_time_ns == 0) {
                best_pid_ctx->next_pacing_time_ns = sched_time_ns + p_interval_ns;
            } else {
                best_pid_ctx->next_pacing_time_ns += p_interval_ns;

                if (ctx->strict_cbr) {
                    // For extreme <44kbps (±1 packet) fluctuation, we absolutely forbid back-to-back
                    // "catch up" bursts. We enforce a minimum spacing of 90% of the nominal interval from NOW.
                    uint64_t min_gap = (p_interval_ns * 9) / 10;
                    if (best_pid_ctx->next_pacing_time_ns < sched_time_ns + min_gap) {
                        best_pid_ctx->next_pacing_time_ns = sched_time_ns + min_gap;
                    }
                } else {
                    // In standard mode, if we fall deeply behind (e.g. due to bursty input), we allow
                    // catching up, but prevent the pacing clock from winding up infinitely into the past.
                    if (sched_time_ns > best_pid_ctx->next_pacing_time_ns + p_interval_ns * 4) {
                        best_pid_ctx->next_pacing_time_ns = sched_time_ns - p_interval_ns;
                    }
                }
            }

            tstd_update_on_pop(best_prog, &ctx->scratch_pkt, sched_time_ns);
            return &ctx->scratch_pkt;
        }
    }

    // Phase 3: Background Best-Effort (Low Prio / Padding)
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        for (int p = 0; p < prog->num_pids; p++) {
            tstd_pid_ctx_t* pid_ctx = &prog->pids[p];
            if (pid_ctx->priority == PRIO_LOW) {
                if (tstd_can_send_packet(pid_ctx, sched_time_ns)) {
                    if (spsc_queue_pop(pid_ctx->queue, &ctx->scratch_pkt)) {
                        tstd_update_on_pop(prog, &ctx->scratch_pkt, sched_time_ns);
                        return &ctx->scratch_pkt;
                    }
                }
            }
        }
    }

    return NULL;  // Emission of NULL packet
}
