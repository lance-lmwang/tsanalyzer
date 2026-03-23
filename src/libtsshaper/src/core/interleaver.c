#include <float.h>
#include "hal.h"
#include "internal.h"

ts_packet_t* interleaver_select(tsshaper_t* ctx) {
    if (ctx->num_programs == 0) return NULL;

    // Phase 1: Critical Control Traffic (PAT/PMT/PCR)
    // Absolute priority to meet TR 101 290 P1/P2 timing constraints.
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        if (spsc_queue_count(prog->queues[PRIO_CRITICAL]) > 0) {
            if (spsc_queue_pop(prog->queues[PRIO_CRITICAL], &ctx->scratch_pkt) == 0) {
                tstd_update_on_pop(prog, &ctx->scratch_pkt, hal_get_time_ns());
                return &ctx->scratch_pkt;
            }
        }
    }

    // Phase 2: WFQ for Audio & Video
    // We aim for perfectly interleaved output to minimize per-PID bitrate jitter.
    program_ctx_t* best_prog = NULL;
    packet_prio_t best_prio = MAX_PRIO;
    double min_vtime = DBL_MAX;

    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        // Unified scheduler for all media queues
        for (packet_prio_t p = PRIO_HIGH; p <= PRIO_MEDIUM; p++) {
            if (spsc_queue_count(prog->queues[p]) > 0) {
                // Heuristic: intrinsic weights based on priority
                // High (Audio) gets 8x preference over Medium (Video) in virtual time
                double weight_factor = (p == PRIO_HIGH) ? 8.0 : 1.0;
                double effective_vtime = prog->wfq_vtime / weight_factor;

                if (effective_vtime < min_vtime) {
                    min_vtime = effective_vtime;
                    best_prog = prog;
                    best_prio = p;
                }
            }
        }
    }

    if (best_prog) {
        if (spsc_queue_pop(best_prog->queues[best_prio], &ctx->scratch_pkt) == 0) {
            tstd_update_on_pop(best_prog, &ctx->scratch_pkt, hal_get_time_ns());

            // Per-packet increment based on overall program weight
            best_prog->wfq_vtime += (double)TS_PACKET_SIZE * 8 / best_prog->wfq_weight;
            return &ctx->scratch_pkt;
        }
    }

    // Phase 3: Background data
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active || spsc_queue_count(prog->queues[PRIO_LOW]) == 0) continue;

        if (spsc_queue_pop(prog->queues[PRIO_LOW], &ctx->scratch_pkt) == 0) {
            tstd_update_on_pop(prog, &ctx->scratch_pkt, hal_get_time_ns());
            return &ctx->scratch_pkt;
        }
    }

    return NULL; // Trigger NULL packet insertion for CBR
}
