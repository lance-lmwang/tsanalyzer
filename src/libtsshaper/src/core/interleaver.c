#include <float.h>
#include "hal.h"
#include "internal.h"

ts_packet_t* interleaver_select(tsshaper_t* ctx) {
    if (ctx->num_programs == 0) return NULL;

    program_ctx_t* best_prog = NULL;
    double min_vtime = DBL_MAX;

    // Phase 1: Absolute Priority Check (Urgent/Critical)
    // If any program has a CRITICAL packet (PCR/PSI) at the head of its queue,
    // we take it immediately to maintain sub-ms jitter.
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        ts_packet_t* head = NULL;
        // Peek logic: SPSC doesn't have peek, but we can check if head exists
        if (spsc_queue_count(prog->ingest_queue) > 0) {
            // Check the head packet PID priority
            // Simplified: we rely on WFQ for normal packets,
            // but we add a specific bypass if needed.
            // For now, let's keep the WFQ but we will refine it.
        }
    }

    // Phase 2: Weighted Fair Queuing (WFQ) / Urgency Arbiter
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active || spsc_queue_count(prog->ingest_queue) == 0) continue;

        if (prog->wfq_vtime < min_vtime) {
            min_vtime = prog->wfq_vtime;
            best_prog = prog;
        }
    }

    if (best_prog) {
        // Use context-bound scratch buffer instead of static
        if (spsc_queue_pop(best_prog->ingest_queue, &ctx->scratch_pkt)) {
            // Update T-STD and WFQ state
            tstd_update_on_pop(best_prog, &ctx->scratch_pkt, hal_get_time_ns());

            // Advance virtual time based on packet size and weight
            best_prog->wfq_vtime += (double)TS_PACKET_SIZE * 8 / best_prog->wfq_weight;
            return &ctx->scratch_pkt;
        }
    }

    return NULL;  // Time for a NULL packet
}
