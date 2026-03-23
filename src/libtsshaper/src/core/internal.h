#ifndef LIBTSSHAPER_INTERNAL_H
#define LIBTSSHAPER_INTERNAL_H

#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/socket.h>
#include "spsc_queue.h"
#include "tsshaper/tsshaper.h"

struct mmsghdr; // Forward declaration

#define MAX_PROGRAMS 128
#define MAX_PIDS_PER_PROGRAM 64
#define NS_PER_SEC 1000000000ULL
#define TS_SYSTEM_CLOCK_HZ 27000000ULL

#define TSTD_HWM_PERCENT 95

// ============================================================================
// Q16.16 Fixed-Point Math (For Deterministic Hot-Path Control)
// ============================================================================
#define Q16_SHIFT 16
#define FLOAT_TO_Q16(f) ((int32_t)((f) * (1 << Q16_SHIFT)))
#define Q16_TO_FLOAT(q) ((float)(q) / (1 << Q16_SHIFT))
#define Q16_MUL(a, b)   ((int32_t)(((int64_t)(a) * (b)) >> Q16_SHIFT))

typedef struct {
    int32_t kp;           // Proportional gain (Q16.16)
    int32_t ki;           // Integral gain (Q16.16)
    int32_t integral;     // Integral accumulator (Q16.16)
    int32_t out_max;      // Output clamp high (Q16.16)
    int32_t out_min;      // Output clamp low (Q16.16)
    int32_t integral_max; // Anti-windup high (Q16.16)
    int32_t integral_min; // Anti-windup low (Q16.16)
    int32_t deadband;     // Error deadband (Bytes)
} __attribute__((aligned(64))) tss_pi_controller_t;

typedef enum {
    PRIO_CRITICAL = 0,  // PCR / PSI
    PRIO_HIGH,          // Audio
    PRIO_MEDIUM,        // Video
    PRIO_LOW,           // Other
    PRIO_PADDING,       // NULL
    MAX_PRIO
} packet_prio_t;

typedef struct {
    uint16_t pid;
    uint32_t buffer_size;
    _Atomic uint32_t buffer_fullness;
    uint64_t leak_rate_bps;
    packet_prio_t priority;
    uint64_t last_update_ns;
    bool is_pcr;
    uint8_t last_cc;
    bool first_packet;

    // Traffic Shaping (Leaky Bucket)
    double shaping_credit_bits;
    uint64_t shaping_rate_bps;
} tstd_pid_ctx_t;

typedef struct {
    int program_id;
    uint64_t target_bitrate_bps;
    uint64_t current_bitrate_bps;
    double complexity;

    tss_pi_controller_t pi; // Fixed-point bitrate controller

    spsc_queue_t* queues[MAX_PRIO];
    tstd_pid_ctx_t pids[MAX_PIDS_PER_PROGRAM];
    int num_pids;
    bool active;
    double wfq_vtime;
    double wfq_weight;
    double queue_vtime[MAX_PRIO];
    void* parent_ctx;  // Pointer to tsshaper_t
} program_ctx_t;

/**
 * @brief Opaque handle implementation.
 */
struct tsshaper_ctx {
    uint64_t total_bitrate_bps;
    uint64_t packet_interval_ns;
    uint32_t io_batch_size;

    program_ctx_t programs[MAX_PROGRAMS];
    int num_programs;

    // Pacer loop state
    pthread_t pacer_thread;
    volatile bool running;

    // Platform HAL config
    int cpu_affinity;
    int sched_priority;
    bool use_raw_clock;

    // Generic Output Backend
    void* backend_priv;

    // HAL Ops for Backend Dispatching (Professional Abstraction)
    struct {
        int (*io_init)(tsshaper_t* ctx, void* params);
        int (*io_send)(tsshaper_t* ctx, struct mmsghdr* msgs, int count);
        void (*io_close)(tsshaper_t* ctx);
    } hal_ops;

    // Stats
    _Atomic uint64_t bytes_sent;
    _Atomic uint64_t pcr_count;
    _Atomic uint64_t null_packets_inserted;

    // Logging
    tss_log_cb log_cb;
    void* log_opaque;

    // Internal timing
    uint64_t next_packet_time_ns;
    uint64_t ideal_packet_time_ns;  // Theoretical CBR grid
    uint64_t start_time_ns;
    uint64_t start_pcr_base;  // Base PCR value from the first packet

    // NULL packet template
    uint8_t null_pkt[TS_PACKET_SIZE];

    // Pacer clock stability
    tss_pi_controller_t pacer_pi;

    // Scratch buffer for interleaver (avoid static)
    ts_packet_t scratch_pkt;
};

// Internal Logging Helper
void tss_log_impl(tsshaper_t* ctx, tss_log_level_t level, const char* fmt, ...);

// PI Controller
void tss_pi_init(tss_pi_controller_t* pi, float kp, float ki,
                 float out_max, float out_min, float int_max, float int_min);
int32_t tss_pi_update(tss_pi_controller_t* pi, int32_t error_q16);

#define tss_error(ctx, fmt, ...) tss_log_impl(ctx, TSS_LOG_ERROR, fmt, ##__VA_ARGS__)
#define tss_warn(ctx, fmt, ...) tss_log_impl(ctx, TSS_LOG_WARN, fmt, ##__VA_ARGS__)
#define tss_info(ctx, fmt, ...) tss_log_impl(ctx, TSS_LOG_INFO, fmt, ##__VA_ARGS__)
#define tss_debug(ctx, fmt, ...) tss_log_impl(ctx, TSS_LOG_DEBUG, fmt, ##__VA_ARGS__)

// Internal functions
void tstd_update_on_push(program_ctx_t* prog, const ts_packet_t* pkt);
void tstd_update_on_pop(program_ctx_t* prog, const ts_packet_t* pkt, uint64_t now_ns);
bool tstd_check_backpressure(program_ctx_t* prog, uint16_t pid);
void statmux_rebalance(tsshaper_t* ctx);
ts_packet_t* interleaver_select(tsshaper_t* ctx);

#endif  // LIBTSSHAPER_INTERNAL_H
