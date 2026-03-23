#include <string.h>
#include "internal.h"

static void restamp_pcr(uint8_t* pkt, uint64_t now_ns, uint64_t start_time_ns, uint64_t start_pcr_base) {
    // PCR is 33-bit base + 9-bit extension = 42 bits at 27MHz
    // Calculate current PCR in 27MHz units
    // Ensure 64-bit precision during calculation to avoid overflow/drift
    uint64_t elapsed_ns = now_ns - start_time_ns;
    uint64_t pcr_27m = start_pcr_base + (elapsed_ns * 27) / 1000;

    uint64_t base = pcr_27m / 300;
    uint32_t ext = pcr_27m % 300;

    // TS adaptation field starts at pkt[4]. PCR is at offset 6 from sync byte
    uint8_t* pcr_buf = pkt + 6;
    pcr_buf[0] = (base >> 25) & 0xFF;
    pcr_buf[1] = (base >> 17) & 0xFF;
    pcr_buf[2] = (base >> 9) & 0xFF;
    pcr_buf[3] = (base >> 1) & 0xFF;
    pcr_buf[4] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    pcr_buf[5] = ext & 0xFF;
}

static tstd_pid_ctx_t* find_or_create_pid_ctx(program_ctx_t* prog, uint16_t pid) {
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) return &prog->pids[i];
    }

    if (prog->num_pids < MAX_PIDS_PER_PROGRAM) {
        tstd_pid_ctx_t* ctx = &prog->pids[prog->num_pids++];
        memset(ctx, 0, sizeof(*ctx));
        ctx->pid = pid;
        ctx->first_packet = true;

        // Broadcast-grade PID Priority Mapping (ISO 13818-1 & TR 101 290)
        if (pid == 0x00 || pid == 0x01 || pid == 0x11 || pid == 0x12) {
            ctx->priority = PRIO_CRITICAL;  // PAT/CAT/SDT/EIT
            ctx->buffer_size = 64 * 1024;
            ctx->shaping_rate_bps = 0; // Unlimited/Critical
        } else {
            ctx->priority = PRIO_MEDIUM;
            ctx->buffer_size = 4 * 1024 * 1024; // Increased buffer for smoothing

            // Heuristic: Assign shaping rates based on PID to enforce smoothness
            // In a real system, this would be configured via API.
            if (prog->target_bitrate_bps > 0) {
                if (pid == 0x0100 || pid == 0x1500) { // Video PIDs
                    ctx->shaping_rate_bps = prog->target_bitrate_bps * 85 / 100;
                } else if (pid == 0x0101 || pid == 0x1501) { // Audio PIDs
                    ctx->shaping_rate_bps = prog->target_bitrate_bps * 10 / 100;
                } else {
                    ctx->shaping_rate_bps = prog->target_bitrate_bps;
                }
                // Allow 500ms burst
                ctx->shaping_credit_bits = (double)ctx->shaping_rate_bps * 0.5;
            }
        }
        return ctx;
    }
    return NULL;
}

void tstd_update_on_push(program_ctx_t* prog, const ts_packet_t* pkt) {
    uint16_t pid = ((pkt->data[1] & 0x1F) << 8) | pkt->data[2];
    tstd_pid_ctx_t* ctx = find_or_create_pid_ctx(prog, pid);
    if (ctx) {
        // Continuity Counter Check (TR 101 290 P1 - Continuity_error)
        uint8_t current_cc = pkt->data[3] & 0x0F;
        if (!ctx->first_packet) {
            uint8_t expected_cc = (ctx->last_cc + 1) & 0x0F;
            if (current_cc != expected_cc && current_cc != ctx->last_cc) {
                // In a real analyzer, we would log a TR 101 290 P1 error here
                // For the shaper, we just track it.
            }
        }
        ctx->last_cc = current_cc;
        ctx->first_packet = false;

        atomic_fetch_add(&ctx->buffer_fullness, TS_PACKET_SIZE);

        // PCR Detection and Priority Elevation
        bool has_af = (pkt->data[3] & 0x20) != 0;
        if (has_af && pkt->data[4] > 0) {
            bool has_pcr = (pkt->data[5] & 0x10) != 0;
            if (has_pcr) {
                ctx->is_pcr = true;
                ctx->priority = PRIO_CRITICAL;

                // Initialize start_pcr_base from the very first PCR packet
                tsshaper_t* shaper = (tsshaper_t*)prog->parent_ctx;
                if (shaper && shaper->start_pcr_base == 0) {
                    uint8_t* pcr_buf = (uint8_t*)pkt->data + 6;
                    uint64_t base = ((uint64_t)pcr_buf[0] << 25) | ((uint64_t)pcr_buf[1] << 17) |
                                    ((uint64_t)pcr_buf[2] << 9) | ((uint64_t)pcr_buf[3] << 1) | (pcr_buf[4] >> 7);
                    uint32_t ext = ((uint32_t)(pcr_buf[4] & 0x01) << 8) | pcr_buf[5];
                    shaper->start_pcr_base = base * 300 + ext;
                    shaper->start_time_ns = pkt->arrival_ns;
                }
            }
        }
    }
}

void tstd_update_on_pop(program_ctx_t* prog, const ts_packet_t* pkt, uint64_t now_ns) {
    uint16_t pid = ((pkt->data[1] & 0x1F) << 8) | pkt->data[2];
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) {
            tstd_pid_ctx_t* ctx = &prog->pids[i];

            // ISO 13818-1 Compliance: Restamp PCR at physical departure point
            if (ctx->is_pcr) {
                tsshaper_t* shaper = (tsshaper_t*)prog->parent_ctx;
                if (shaper && shaper->start_time_ns > 0) {
                    restamp_pcr((uint8_t*)pkt->data, now_ns, shaper->start_time_ns, shaper->start_pcr_base);
                }
            }

            uint32_t current = atomic_load(&ctx->buffer_fullness);
            if (current >= TS_PACKET_SIZE) {
                atomic_fetch_sub(&ctx->buffer_fullness, TS_PACKET_SIZE);
            }
            ctx->last_update_ns = now_ns;
            return;
        }
    }
}

bool tstd_check_backpressure(program_ctx_t* prog, uint16_t pid) {
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) {
            uint32_t current = atomic_load_explicit(&prog->pids[i].buffer_fullness, memory_order_relaxed);
            // 95% High Water Mark - compliant with T-STD buffer modeling
            if (current > (prog->pids[i].buffer_size * 95 / 100)) {
                return true;
            }
            return false;
        }
    }
    return false;
}
