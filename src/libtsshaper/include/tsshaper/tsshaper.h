/**
 * @file tsshaper.h
 * @brief Professional MPEG-TS Traffic Shaper & T-STD Engine (ABI Stable)
 *
 * libtsshaper provides hardware-level precision for TS multiplexing,
 * ensuring TR 101 290 compliance and sub-microsecond jitter.
 */

#ifndef TSSHAPER_H
#define TSSHAPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Unified nanosecond time domain (based on CLOCK_MONOTONIC_RAW).
 */
typedef int64_t tss_time_ns;

/**
 * @brief Opaque handle for the shaper context.
 */
typedef struct tsshaper_ctx tsshaper_t;

/**
 * @brief Return value when no packets are available in the scheduler.
 */
#define TSS_IDLE -1

/**
 * @brief Output backend types.
 */
typedef enum {
    TSS_BACKEND_REAL_NETWORK = 0, /**< Standard sendmmsg on Linux */
    TSS_BACKEND_VIRTUAL_PCAP,     /**< Write to PCAP file (Virtual Time Domain) */
    TSS_BACKEND_CALLBACK          /**< User-provided callback (for FFmpeg AVIOContext) */
} tss_backend_type_t;

/**
 * @brief User-provided write callback signature.
 * @param pkt Pointer to the 188-byte TS packet.
 * @param opaque User data pointer.
 * @return 0 on success, < 0 on error.
 */
typedef int (*tss_write_cb)(const uint8_t* pkt, void* opaque);

/**
 * @brief Configuration for libtsshaper initialization.
 */
typedef struct {
    uint64_t bitrate_bps;     /**< Target CBR bitrate in bits per second */
    uint32_t pcr_interval_ms; /**< Max interval between PCR anchors (default: 35) */
    uint32_t io_batch_size;   /**< Number of packets per sendmmsg (default: 7) */
    uint32_t max_latency_ms;  /**< Internal buffer depth (backpressure threshold) */
    bool use_raw_clock;       /**< Use CLOCK_MONOTONIC_RAW for zero-jitter timing */

    tss_backend_type_t backend;     /**< Selected output method */
    void*              backend_params; /**< Opaque params (e.g. filename string for PCAP) */

    tss_write_cb write_cb;     /**< Callback for TSS_BACKEND_CALLBACK */
    void*        write_opaque; /**< Opaque data for write_cb */
} tsshaper_config_t;

/**
 * @brief Create a new shaper instance.
 * @param cfg Configuration struct.
 * @return Handle to the shaper, or NULL on memory failure.
 */
tsshaper_t* tsshaper_create(const tsshaper_config_t* cfg);

/**
 * @brief Destroy the shaper instance and free all resources.
 */
void tsshaper_destroy(tsshaper_t* ctx);

/**
 * @brief Push a VBR TS packet into the shaper (Non-blocking).
 *
 * If the internal T-STD buffer reaches the High-Water Mark (95%),
 * this function will return -1 to signal backpressure to the source.
 *
 * @param ctx Shaper handle.
 * @param pid The PID of the packet (for quick routing/T-STD tracking).
 * @param pkt Pointer to the 188-byte TS packet data.
 * @param arrival_ts External timestamp (ns). If 0, the library uses internal clock.
 * @return 0 on success, -1 if the buffer is full (Backpressure).
 */
int tsshaper_push(tsshaper_t* ctx, uint16_t pid, const uint8_t* pkt, tss_time_ns arrival_ts);

/**
 * @brief Pull the next scheduled packet from the interleaver (Synchronous Mode).
 *
 * This is used for custom pacer loops.
 *
 * @param ctx Shaper handle.
 * @param out_pkt Pointer to a 188-byte buffer to receive the packet.
 * @return The target emission time (T_emit) in ns, or TSS_IDLE if queue is empty.
 */
tss_time_ns tsshaper_pull(tsshaper_t* ctx, uint8_t* out_pkt);

/**
 * @brief Start the internal asynchronous pacer thread (High-Priority/Real-time).
 *
 * The thread will run with SCHED_FIFO policy (if permitted) and perform
 * precision busy-wait I/O.
 *
 * @param ctx Shaper handle.
 * @param fd The output file descriptor (socket, file, or pipe).
 * @return 0 on success, < 0 on thread creation error.
 */
int tsshaper_start_pacer(tsshaper_t* ctx, int fd);

/**
 * @brief Stop the asynchronous pacer thread.
 */
void tsshaper_stop_pacer(tsshaper_t* ctx);

/**
 * @brief Runtime statistics for monitoring.
 */
typedef struct {
    double current_bitrate_bps;     /**< Measured output bitrate */
    uint32_t buffer_fullness_pct;   /**< Current T-STD buffer level (0-100) */
    double pcr_jitter_ns;           /**< Estimated PCR jitter deviation */
    uint64_t null_packets_inserted; /**< Total padding packets generated */
    uint64_t continuity_errors;     /**< Cumulative ingest errors */
} tsshaper_stats_t;

/**
 * @brief Retrieve current metrics from the shaper.
 */
void tsshaper_get_stats(tsshaper_t* ctx, tsshaper_stats_t* stats);

/**
 * @brief Log levels for the shaper library.
 */
typedef enum { TSS_LOG_ERROR = 0, TSS_LOG_WARN, TSS_LOG_INFO, TSS_LOG_DEBUG, TSS_LOG_TRACE } tss_log_level_t;

/**
 * @brief Log callback function signature.
 * @param level Log severity.
 * @param msg The log message.
 * @param opaque User data provided at registration.
 */
typedef void (*tss_log_cb)(tss_log_level_t level, const char* msg, void* opaque);

/**
 * @brief Set a custom logging callback.
 *
 * If not set, the library will log to stderr by default (if built with debug).
 *
 * @param ctx Shaper handle.
 * @param cb The callback function.
 * @param opaque User data pointer passed to the callback.
 */
void tsshaper_set_log_callback(tsshaper_t* ctx, tss_log_cb cb, void* opaque);

/**
 * @brief Set the target bitrate (leak rate) for a specific PID.
 *
 * According to ISO/IEC 13818-1 T-STD model, data enters the Transport Buffer (TB)
 * at the channel rate but leaves at the leak rate (Rx). Since TB is small (512 bytes),
 * the input rate must effectively match the leak rate to avoid overflow.
 *
 * @param ctx Shaper handle.
 * @param pid The PID to configure.
 * @param bitrate_bps The target shaping rate for this PID (e.g. 1.2Mbps for a 1Mbps video).
 *                    Set to 0 to auto-share remaining bandwidth (default).
 * @return 0 on success, -1 if PID context not found (push a packet first or add program manually).
 */
int tsshaper_set_pid_bitrate(tsshaper_t* ctx, uint16_t pid, uint64_t bitrate_bps);

#ifdef __cplusplus
}
#endif

#endif  // TSSHAPER_H
