#include <string.h>
#include "internal.h"

static void restamp_pcr(uint8_t* pkt, uint64_t now_ns, uint64_t start_time_ns, uint64_t start_pcr_base) {
    // PCR is 33-bit base + 9-bit extension = 42 bits at 27MHz
    // Calculate current PCR in 27MHz units
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

        // Broadcast-grade PID Priority Mapping
        if (pid == 0x00 || pid == 0x01 || pid == 0x11 || pid == 0x12) {
            ctx->priority = PRIO_CRITICAL;  // PAT/CAT/SDT/EIT
            ctx->buffer_size = 64 * 1024;
        } else {
            ctx->priority = PRIO_MEDIUM;
            ctx->buffer_size = 1024 * 1024;
        }
        return ctx;
    }
    return NULL;
}

void tstd_update_on_push(program_ctx_t* prog, const ts_packet_t* pkt) {
    uint16_t pid = ((pkt->data[1] & 0x1F) << 8) | pkt->data[2];
    tstd_pid_ctx_t* ctx = find_or_create_pid_ctx(prog, pid);
    if (ctx) {
        atomic_fetch_add(&ctx->buffer_fullness, TS_PACKET_SIZE);

        // Check if it has PCR
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

            // If this is a PCR packet, we RESTAMP it with exact physical time
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
            // 95% High Water Mark
            if (current > (prog->pids[i].buffer_size * 95 / 100)) {
                return true;
            }
            return false;
        }
    }
    return false;
}
