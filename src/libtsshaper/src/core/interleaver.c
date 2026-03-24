#include <float.h>
#include "hal.h"
#include "internal.h"

static void update_shaping_credits(program_ctx_t* prog, uint64_t now_ns) {
    for (int i = 0; i < prog->num_pids; i++) {
        tstd_pid_ctx_t* ctx = &prog->pids[i];
        if (ctx->shaping_rate_bps == 0) continue; // Unlimited

        if (ctx->last_update_ns == 0) {
            ctx->last_update_ns = now_ns;
            continue;
        }

        uint64_t diff_ns = now_ns - ctx->last_update_ns;
        double added_bits = (double)ctx->shaping_rate_bps * diff_ns / 1000000000.0;
        ctx->shaping_credit_bits += added_bits;

        // Clamp burst credit to 50ms worth of data to prevent huge initial bursts
        double max_credit = (double)ctx->shaping_rate_bps * 0.05;
        if (ctx->shaping_credit_bits > max_credit) {
            ctx->shaping_credit_bits = max_credit;
        }
        ctx->last_update_ns = now_ns;
    }
}

ts_packet_t* interleaver_select(tsshaper_t* ctx) {
    if (ctx->num_programs == 0) return NULL;

    // Use ideal time grid for scheduling to ensure deterministic smoothing
    uint64_t sched_time_ns = ctx->ideal_packet_time_ns > 0 ?
                             ctx->ideal_packet_time_ns : hal_get_time_ns();

    // Phase 0: Update shaping credits for all PIDs based on virtual time
    for (int i = 0; i < ctx->num_programs; i++) {
        update_shaping_credits(&ctx->programs[i], sched_time_ns);
    }

    // Phase 1: Critical Control Traffic (PAT/PMT/PCR)
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        if (spsc_queue_count(prog->queues[PRIO_CRITICAL]) > 0) {
            if (spsc_queue_pop(prog->queues[PRIO_CRITICAL], &ctx->scratch_pkt)) {
                tstd_update_on_pop(prog, &ctx->scratch_pkt, sched_time_ns);
                return &ctx->scratch_pkt;
            }
        }
    }

    // Phase 2: WFQ for Audio & Video with Leaky Bucket constraint
    program_ctx_t* best_prog = NULL;
    packet_prio_t best_prio = MAX_PRIO;
    double min_vtime = DBL_MAX;

    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active) continue;

        for (packet_prio_t prio = PRIO_HIGH; prio <= PRIO_MEDIUM; prio++) {
            if (spsc_queue_count(prog->queues[prio]) > 0) {
                // Peek the next packet to check its PID and shaping credit
                if (!spsc_queue_peek(prog->queues[prio], &ctx->scratch_pkt)) {
                    continue;
                }

                uint16_t head_pid = ((ctx->scratch_pkt.data[1] & 0x1F) << 8) | ctx->scratch_pkt.data[2];
                bool has_credit = true;

                for (int p = 0; p < prog->num_pids; p++) {
                    if (prog->pids[p].pid == head_pid && prog->pids[p].shaping_rate_bps > 0) {
                        if (prog->pids[p].shaping_credit_bits < (TS_PACKET_SIZE * 8)) {
                            // printf("PID %d blocked. Credit: %.2f needed %d\n", head_pid, prog->pids[p].shaping_credit_bits, TS_PACKET_SIZE * 8);
                            has_credit = false;
                        }
                        break;
                    }
                }

                if (has_credit && prog->queue_vtime[prio] < min_vtime) {
                    min_vtime = prog->queue_vtime[prio];
                    best_prog = prog;
                    best_prio = prio;
                }
            }
        }
    }

    if (best_prog) {
        if (spsc_queue_pop(best_prog->queues[best_prio], &ctx->scratch_pkt)) {
            uint16_t pid = ((ctx->scratch_pkt.data[1] & 0x1F) << 8) | ctx->scratch_pkt.data[2];

            // Deduct shaping credit
            for (int p = 0; p < best_prog->num_pids; p++) {
                if (best_prog->pids[p].pid == pid && best_prog->pids[p].shaping_rate_bps > 0) {
                    best_prog->pids[p].shaping_credit_bits -= (TS_PACKET_SIZE * 8);
                }
            }

            tstd_update_on_pop(best_prog, &ctx->scratch_pkt, sched_time_ns);

            double weight = (best_prio == PRIO_HIGH) ? 1.0 : 20.0;
            best_prog->queue_vtime[best_prio] += (double)TS_PACKET_SIZE * 8 / weight;
            return &ctx->scratch_pkt;
        }
    }

    // Phase 3: Background data (also shaped)
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active || spsc_queue_count(prog->queues[PRIO_LOW]) == 0) continue;

        if (spsc_queue_pop(prog->queues[PRIO_LOW], &ctx->scratch_pkt)) {
            tstd_update_on_pop(prog, &ctx->scratch_pkt, sched_time_ns);
            return &ctx->scratch_pkt;
        }
    }

    return NULL; // Trigger NULL packet insertion for CBR
}
