#ifndef LIBTSSHAPER_INTERNAL_H
#define LIBTSSHAPER_INTERNAL_H

#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/socket.h>
#include "spsc_queue.h"
#include "tsshaper/tsshaper.h"

#define MAX_PROGRAMS 128
#define MAX_PIDS_PER_PROGRAM 64
#define NS_PER_SEC 1000000000ULL
#define TS_SYSTEM_CLOCK_HZ 27000000ULL

#define TSTD_HWM_PERCENT 95
#define PI_KP 0.1
#define PI_KI 0.01

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
} tstd_pid_ctx_t;

typedef struct {
    int program_id;
    uint64_t target_bitrate_bps;
    uint64_t current_bitrate_bps;
    double complexity;
    double error_sum;
    double last_error;
    spsc_queue_t* ingest_queue;
    tstd_pid_ctx_t pids[MAX_PIDS_PER_PROGRAM];
    int num_pids;
    bool active;
    double wfq_vtime;
    double wfq_weight;
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

    // Output state
    int output_fd;
    struct sockaddr_in output_addr;

    // Stats
    _Atomic uint64_t bytes_sent;
    _Atomic uint64_t pcr_count;
    _Atomic uint64_t null_packets_inserted;

    // Logging
    tss_log_cb log_cb;
    void* log_opaque;

    // Internal timing
    uint64_t next_packet_time_ns;
    uint64_t start_time_ns;

    // NULL packet template
    uint8_t null_pkt[TS_PACKET_SIZE];

    // Scratch buffer for interleaver (avoid static)
    ts_packet_t scratch_pkt;
};

// Internal Logging Helper
void tss_log_impl(tsshaper_t* ctx, tss_log_level_t level, const char* fmt, ...);

#define tss_error(ctx, fmt, ...) tss_log_impl(ctx, TSS_LOG_ERROR, fmt, ##__VA_ARGS__)
#define tss_warn(ctx, fmt, ...)  tss_log_impl(ctx, TSS_LOG_WARN, fmt, ##__VA_ARGS__)
#define tss_info(ctx, fmt, ...)  tss_log_impl(ctx, TSS_LOG_INFO, fmt, ##__VA_ARGS__)
#define tss_debug(ctx, fmt, ...) tss_log_impl(ctx, TSS_LOG_DEBUG, fmt, ##__VA_ARGS__)

// Internal functions
void tstd_update_on_push(program_ctx_t* prog, const ts_packet_t* pkt);
void tstd_update_on_pop(program_ctx_t* prog, const ts_packet_t* pkt, uint64_t now_ns);
bool tstd_check_backpressure(program_ctx_t* prog, uint16_t pid);
void statmux_rebalance(tsshaper_t* ctx);
ts_packet_t* interleaver_select(tsshaper_t* ctx);

#endif  // LIBTSSHAPER_INTERNAL_H
