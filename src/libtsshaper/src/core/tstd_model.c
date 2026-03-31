#include <stdint.h>
#include <string.h>

#include "internal.h"

// ISO/IEC 13818-1 Broadcasting-Grade PCR Restamper
// Ensures nanosecond-precision STC recovery for Professional Receivers

static void restamp_pcr(uint8_t* pkt, uint64_t now_ns, uint64_t start_time_ns, uint64_t start_pcr_base) {
    // 1. Calculate absolute elapsed time in 27MHz ticks
    // (ns * 27) / 1000 is the precise conversion for MPEG-TS STC
    uint64_t elapsed_ns = now_ns - start_time_ns;
    uint64_t ticks_27m = (elapsed_ns * 27) / 1000;
    uint64_t current_pcr_42 = start_pcr_base + ticks_27m;

    // 2. Break down to 33-bit Base and 9-bit Extension
    uint64_t base = current_pcr_42 / 300;
    uint32_t ext = current_pcr_42 % 300;

    // 3. Bit-perfect injection into the Adaptation Field
    // Offset 6: PCR base [32...25]
    // Offset 10: PCR base [0], 6 bits reserved, PCR ext [8]
    uint8_t* p = pkt + 6;
    p[0] = (uint8_t)(base >> 25);
    p[1] = (uint8_t)(base >> 17);
    p[2] = (uint8_t)(base >> 9);
    p[3] = (uint8_t)(base >> 1);
    p[4] = (uint8_t)(((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01));
    p[5] = (uint8_t)(ext & 0xFF);
}

tstd_pid_ctx_t* tstd_find_or_create_pid_ctx(program_ctx_t* prog, uint16_t pid) {
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) return &prog->pids[i];
    }

    if (prog->num_pids < MAX_PIDS_PER_PROGRAM) {
        tstd_pid_ctx_t* ctx = &prog->pids[prog->num_pids++];
        memset(ctx, 0, sizeof(*ctx));
        ctx->pid = pid;
        ctx->first_packet = true;
        ctx->buffer_size = 512;  // Mandatory TBn size

        // BROADCAST GRADE SIGNALING RECOGNITION
        if (pid == 0x00 || pid == 0x01 || pid == 0x10 || pid == 0x11 || pid == 0x12) {
            ctx->priority = PRIO_CRITICAL;
            ctx->shaping_rate_bps = 15000;  // 15kbps reservation for PSI/SI
        } else {
            ctx->priority = PRIO_MEDIUM;
            ctx->shaping_rate_bps = prog->target_bitrate_bps / 2;
        }

        // Initialize pacing windows
        ctx->shaping_credit_bits = (double)TS_PACKET_SIZE * 8;
        ctx->next_pacing_time_ns = 0;
        ctx->queue = spsc_queue_create(1024);  // Give every PID a massive 1024-packet buffer

        return ctx;
    }
    return NULL;
}

void tstd_update_on_push(program_ctx_t* prog, const ts_packet_t* pkt) {
    uint16_t pid = ((pkt->data[1] & 0x1F) << 8) | pkt->data[2];
    tstd_pid_ctx_t* ctx = tstd_find_or_create_pid_ctx(prog, pid);
    if (!ctx) return;

    // 1. Bitrate Estimation (500ms sliding window)
    if (ctx->est_window_start_ns == 0) {
        ctx->est_window_start_ns = pkt->arrival_ns;
        ctx->est_bits_in_window = 0;
    }
    ctx->est_bits_in_window += (TS_PACKET_SIZE * 8);
    uint64_t duration_ns = pkt->arrival_ns - ctx->est_window_start_ns;
    if (duration_ns >= (500 * 1000000ULL)) {
        ctx->measured_bitrate_bps = (uint64_t)((double)ctx->est_bits_in_window * 1000000000.0 / (double)duration_ns);

        // Auto-update shaping rate if not manually locked
        if (ctx->shaping_rate_bps == 0 || ctx->shaping_rate_bps == (prog->target_bitrate_bps / 2)) {
            // Apply a 5% margin to avoid T-STD underflow
            ctx->shaping_rate_bps = (uint64_t)(ctx->measured_bitrate_bps * 1.05);
            // Cap at program rate
            if (ctx->shaping_rate_bps > prog->target_bitrate_bps) {
                ctx->shaping_rate_bps = prog->target_bitrate_bps;
            }
        }

        ctx->est_window_start_ns = pkt->arrival_ns;
        ctx->est_bits_in_window = 0;
    }

    // 2. Detect PCR and initialize Phase Lock
    bool has_af = (pkt->data[3] & 0x20) != 0;
    if (has_af && pkt->data[4] > 0) {
        bool has_pcr = (pkt->data[5] & 0x10) != 0;
        if (has_pcr) {
            ctx->is_pcr = true;
            tsshaper_t* shaper = (tsshaper_t*)prog->parent_ctx;
            if (shaper) {
                if (shaper->master_pcr_pid == 0x1FFF) {
                    shaper->master_pcr_pid = pid;
                }
            }
        }
    }

    ctx->last_cc = pkt->data[3] & 0x0F;
    ctx->first_packet = false;
    atomic_fetch_add(&ctx->buffer_fullness, TS_PACKET_SIZE);

    tsshaper_t* shaper = (tsshaper_t*)prog->parent_ctx;
    if (shaper) {
        shaper->last_arrival_ns = pkt->arrival_ns;
    }
}

void tstd_update_on_pop(program_ctx_t* prog, const ts_packet_t* pkt, uint64_t now_ns) {
    uint16_t pid = ((pkt->data[1] & 0x1F) << 8) | pkt->data[2];
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) {
            tstd_pid_ctx_t* ctx = &prog->pids[i];

            // 1. LEAK RECEIVER TBn BUFFER
            if (ctx->last_tb_update_ns > 0 && now_ns > ctx->last_tb_update_ns) {
                double delta_s = (double)(now_ns - ctx->last_tb_update_ns) / 1000000000.0;
                double leak_bits = delta_s * (double)ctx->shaping_rate_bps;
                ctx->tb_fullness_bits -= leak_bits;
                if (ctx->tb_fullness_bits < 0) ctx->tb_fullness_bits = 0;
            }
            // 2. FILL RECEIVER TBn BUFFER (Packet arrival at receiver)
            ctx->tb_fullness_bits += (188.0 * 8.0);
            ctx->last_tb_update_ns = now_ns;

            if (ctx->is_pcr) {
                tsshaper_t* shaper = (tsshaper_t*)prog->parent_ctx;
                if (shaper) {
                    if (shaper->start_pcr_base == 0) {
                        uint8_t* pb = (uint8_t*)pkt->data + 6;
                        uint64_t base = ((uint64_t)pb[0] << 25) | ((uint64_t)pb[1] << 17) | ((uint64_t)pb[2] << 9) |
                                        ((uint64_t)pb[3] << 1) | (pb[4] >> 7);
                        uint32_t ext = ((uint32_t)(pb[4] & 0x01) << 8) | pb[5];
                        shaper->start_pcr_base = base * 300 + ext;
                        shaper->start_time_ns = now_ns;
                    } else if (shaper->start_time_ns > 0) {
                        restamp_pcr((uint8_t*)pkt->data, now_ns, shaper->start_time_ns, shaper->start_pcr_base);
                    }
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

bool tstd_can_send_packet(tstd_pid_ctx_t* ctx, uint64_t now_ns) {
    if (!ctx->queue || spsc_queue_count(ctx->queue) == 0) return false;
    if (ctx->shaping_rate_bps == 0) return true;  // Best effort

    // 1. LEAK SIMULATION
    double current_tb_bits = ctx->tb_fullness_bits;
    if (ctx->last_tb_update_ns > 0 && now_ns > ctx->last_tb_update_ns) {
        double delta_s = (double)(now_ns - ctx->last_tb_update_ns) / 1000000000.0;
        double leak_bits = delta_s * (double)ctx->shaping_rate_bps;
        current_tb_bits -= leak_bits;
        if (current_tb_bits < 0) current_tb_bits = 0;
    }

    // 2. TBn OVERFLOW PROTECTION (ISO/IEC 13818-1)
    // TBn size is 512 bytes. We cannot send if it would overflow.
    if (current_tb_bits + (188.0 * 8.0) > (512.0 * 8.0)) {
        return false;
    }

    // 2.1 PANIC MODE (95% HWM)
    // If TB is nearly full, we apply an aggressive pacing penalty to favor other PIDs
    // and force the shaper to "cool down" this specific PID.
    if (current_tb_bits > (512.0 * 8.0 * 0.95)) {
        // Add a 1ms artificial delay to the next pacing time
        if (ctx->next_pacing_time_ns < now_ns + 1000000ULL) {
            ctx->next_pacing_time_ns = now_ns + 1000000ULL;
        }
        return false;
    }

    // 3. LEAKY BUCKET CREDIT CHECK (Pacing)
    if (ctx->next_pacing_time_ns > 0 && now_ns < ctx->next_pacing_time_ns) {
        // printf("[DEBUG] PID 0x%x Pacing: now %lu < next %lu\n", ctx->pid, now_ns, ctx->next_pacing_time_ns);
        return false;
    }

    return true;
}

bool tstd_check_backpressure(program_ctx_t* prog, uint16_t pid) {
    for (int i = 0; i < prog->num_pids; i++) {
        if (prog->pids[i].pid == pid) {
            tstd_pid_ctx_t* ctx = &prog->pids[i];
            if (!ctx->queue) return false;

            // SHAPER INGRESS BACKPRESSURE
            // We use the SPSC queue capacity as the primary backpressure signal.
            // If the queue is > 90% full, we tell the upstream (FFmpeg) to slow down.
            uint32_t count = spsc_queue_count(ctx->queue);
            uint32_t capacity = 1024;  // Defined in tstd_find_or_create_pid_ctx

            if (count > (capacity * 9) / 10) {
                return true;
            }
            return false;
        }
    }
    return false;
}

void tss_pi_init(tss_pi_controller_t* pi, float kp, float ki, float out_max, float out_min, float int_max,
                 float int_min) {
    memset(pi, 0, sizeof(*pi));
    pi->kp = FLOAT_TO_Q16(kp);
    pi->ki = FLOAT_TO_Q16(ki);
    pi->out_max = FLOAT_TO_Q16(out_max);
    pi->out_min = FLOAT_TO_Q16(out_min);
    pi->integral_max = FLOAT_TO_Q16(int_max);
    pi->integral_min = FLOAT_TO_Q16(int_min);
    pi->deadband = 188;
}

int32_t tss_pi_update(tss_pi_controller_t* pi, int32_t error_q16) {
    if (error_q16 > -(pi->deadband << 16) && error_q16 < (pi->deadband << 16)) error_q16 = 0;
    int32_t p_out = Q16_MUL(pi->kp, error_q16);
    pi->integral += error_q16;
    if (pi->integral > pi->integral_max)
        pi->integral = pi->integral_max;
    else if (pi->integral < pi->integral_min)
        pi->integral = pi->integral_min;
    int32_t i_out = Q16_MUL(pi->ki, pi->integral);
    int32_t total = p_out + i_out;
    if (total > pi->out_max)
        total = pi->out_max;
    else if (total < pi->out_min)
        total = pi->out_min;
    return total;
}
