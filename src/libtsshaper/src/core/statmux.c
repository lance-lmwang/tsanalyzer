#include <math.h>
#include "internal.h"

void statmux_rebalance(tsshaper_t* ctx) {
    if (ctx->num_programs == 0) return;

    // If only one program, it gets everything
    if (ctx->num_programs == 1) {
        ctx->programs[0].current_bitrate_bps = ctx->total_bitrate_bps;
        ctx->programs[0].wfq_weight = 1.0;
        return;
    }

    double total_complexity = 0;
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        // Complexity estimation based on all priority queues and PID buffer fullness
        uint32_t total_fullness = 0;
        for (int j = 0; j < prog->num_pids; j++) {
            total_fullness += atomic_load(&prog->pids[j].buffer_fullness);
        }

        uint32_t total_queue_packets = 0;
        for (int p = 0; p < MAX_PRIO; p++) {
            if (prog->queues[p]) total_queue_packets += spsc_queue_count(prog->queues[p]);
        }
        double queue_fill = (double)total_queue_packets / 1024.0;
        double buffer_fill = (double)total_fullness / (2 * 1024 * 1024);  // Based on 2MB avg

        prog->complexity = 0.7 * queue_fill + 0.3 * buffer_fill;
        if (prog->complexity < 0.01) prog->complexity = 0.01;

        total_complexity += prog->complexity;
    }

    if (total_complexity < 0.01) total_complexity = 1.0;

    double total_weight = 0;
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        double target_share = prog->complexity / total_complexity;
        double current_share = prog->wfq_weight;

        double error = target_share - current_share;

        // Use the new Fixed-Point PI Controller for weight adjustment
        int32_t q16_adj = tss_pi_update(&prog->pi, FLOAT_TO_Q16(error));
        prog->wfq_weight += Q16_TO_FLOAT(q16_adj);

        if (prog->wfq_weight < 0.01) prog->wfq_weight = 0.01;
        if (prog->wfq_weight > 1.0) prog->wfq_weight = 1.0;

        total_weight += prog->wfq_weight;
    }

    // Normalize weights and bitrates
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        prog->wfq_weight /= total_weight;
        prog->current_bitrate_bps = (uint64_t)(ctx->total_bitrate_bps * prog->wfq_weight);
    }
}
