#include <string.h>
#include "internal.h"

static tstd_pid_ctx_t* find_or_create_pid_ctx(program_ctx_t* prog, uint16_t pid) {
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) return &prog->pids[i];
    }

    if (prog->num_pids < MAX_PIDS_PER_PROGRAM) {
        tstd_pid_ctx_t* ctx = &prog->pids[prog->num_pids++];
        memset(ctx, 0, sizeof(*ctx));
        ctx->pid = pid;

        // Default buffer size: 1MB for video, 128KB for others
        if (pid == 0 || pid == 0x11 || pid == 0x12) {
            ctx->priority = PRIO_CRITICAL;
            ctx->buffer_size = 128 * 1024;
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
            }
        }
    }
}

void tstd_update_on_pop(program_ctx_t* prog, const ts_packet_t* pkt, uint64_t now_ns) {
    uint16_t pid = ((pkt->data[1] & 0x1F) << 8) | pkt->data[2];
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) {
            tstd_pid_ctx_t* ctx = &prog->pids[i];
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
    // New PID (not tracked yet), assume no backpressure until first push
    return false;
}
