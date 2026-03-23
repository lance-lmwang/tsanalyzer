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
            if (spsc_queue_pop(prog->queues[PRIO_CRITICAL], &ctx->scratch_pkt)) {
                tstd_update_on_pop(prog, &ctx->scratch_pkt, hal_get_time_ns());
                return &ctx->scratch_pkt;
            }
        }
    }

    // Phase 2: WFQ for Audio & Video
    // We use distinct Virtual Times for each queue to ensure fair interleaving.
    program_ctx_t* best_prog = NULL;
    packet_prio_t best_prio = MAX_PRIO;
    double min_vtime = DBL_MAX;

    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        // Check High Priority (Audio)
        if (spsc_queue_count(prog->queues[PRIO_HIGH]) > 0) {
            if (prog->queue_vtime[PRIO_HIGH] < min_vtime) {
                min_vtime = prog->queue_vtime[PRIO_HIGH];
                best_prog = prog;
                best_prio = PRIO_HIGH;
            }
        }

        // Check Medium Priority (Video)
        if (spsc_queue_count(prog->queues[PRIO_MEDIUM]) > 0) {
            if (prog->queue_vtime[PRIO_MEDIUM] < min_vtime) {
                min_vtime = prog->queue_vtime[PRIO_MEDIUM];
                best_prog = prog;
                best_prio = PRIO_MEDIUM;
            }
        }
    }

    if (best_prog) {
        if (spsc_queue_pop(best_prog->queues[best_prio], &ctx->scratch_pkt)) {
            tstd_update_on_pop(best_prog, &ctx->scratch_pkt, hal_get_time_ns());

            // Weight logic:
            // Audio (HIGH) needs low latency but low bandwidth. Weight = 1.0
            // Video (MEDIUM) needs high bandwidth. Weight = 20.0
            // Result: Video vtime grows 20x slower, allowing 20x more packets per "round".
            // But if Audio is available and its vtime lags, it preempts Video.
            double weight = (best_prio == PRIO_HIGH) ? 1.0 : 20.0;

            best_prog->queue_vtime[best_prio] += (double)TS_PACKET_SIZE * 8 / weight;

            // Prevent vtime overflow/drift
            if (best_prog->queue_vtime[best_prio] > 1e14) {
                double decrement = best_prog->queue_vtime[best_prio];
                best_prog->queue_vtime[PRIO_HIGH] -= decrement;
                best_prog->queue_vtime[PRIO_MEDIUM] -= decrement;
                if (best_prog->queue_vtime[PRIO_HIGH] < 0) best_prog->queue_vtime[PRIO_HIGH] = 0;
                if (best_prog->queue_vtime[PRIO_MEDIUM] < 0) best_prog->queue_vtime[PRIO_MEDIUM] = 0;
            }

            return &ctx->scratch_pkt;
        }
    }

    // Phase 3: Background data
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active || spsc_queue_count(prog->queues[PRIO_LOW]) == 0) continue;

        if (spsc_queue_pop(prog->queues[PRIO_LOW], &ctx->scratch_pkt)) {
            tstd_update_on_pop(prog, &ctx->scratch_pkt, hal_get_time_ns());
            return &ctx->scratch_pkt;
        }
    }

    return NULL; // Trigger NULL packet insertion for CBR
}
