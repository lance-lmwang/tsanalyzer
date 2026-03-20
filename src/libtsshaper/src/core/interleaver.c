#include "internal.h"
#include <float.h>
#include <string.h>

ts_packet_t* interleaver_select(tsa_shaper_t* ctx) {
    program_ctx_t* best_prog = NULL;
    double min_vtime = DBL_MAX;

    // Select the program that should send next based on WFQ virtual time
    for (int i = 0; i < ctx->num_programs; i++) {
        program_ctx_t* prog = &ctx->programs[i];
        if (!prog->active || spsc_queue_is_empty(prog->ingest_queue)) continue;

        if (prog->wfq_vtime < min_vtime) {
            min_vtime = prog->wfq_vtime;
            best_prog = prog;
        }
    }

    static ts_packet_t out_pkt;

    if (best_prog) {
        if (spsc_queue_pop(best_prog->ingest_queue, &out_pkt)) {
            // Update virtual time for WFQ
            // The increment is inversely proportional to the weight
            double weight = best_prog->wfq_weight;
            if (weight < 0.001) weight = 0.001;
            best_prog->wfq_vtime += (1.0 / weight);

            // Update T-STD model
            tstd_update_on_pop(best_prog, &out_pkt, hal_get_time_ns());

            return &out_pkt;
        }
    }

    // If no packets available, generate a NULL packet to maintain CBR
    memset(out_pkt.data, 0, TS_PACKET_SIZE);
    out_pkt.data[0] = 0x47;
    out_pkt.data[1] = 0x1F; // PID 0x1FFF (NULL)
    out_pkt.data[2] = 0xFF;
    out_pkt.data[3] = 0x10; // Payload only, CC 0

    // Optional: increment CC for NULL packets? Usually not required for PID 0x1FFF

    out_pkt.timestamp_ns = hal_get_time_ns();

    return &out_pkt;
}
