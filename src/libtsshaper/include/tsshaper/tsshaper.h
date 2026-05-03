/**
 * @file tsshaper.h
 * @brief Professional MPEG-TS T-STD Sync Engine & Traffic Shaper
 *
 * This library provides a standalone, 1:1 port of the industrial-grade
 * T-STD multiplexer logic, decoupled from any specific framework.
 */

#ifndef TSSHAPER_H
#define TSSHAPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TSS_TS_PACKET_SIZE 188
#define TSS_NOPTS_VALUE    ((int64_t)UINT64_C(0x8000000000000000))

typedef struct tsshaper_ctx tsshaper_t;

typedef enum {
    TSS_PID_VIDEO = 0,
    TSS_PID_AUDIO,
    TSS_PID_PSI,
    TSS_PID_DATA,
    TSS_PID_PCR
} tss_pid_type_t;

/**
 * @brief Output callback for shaped packets.
 */
typedef int (*tss_write_cb)(void* opaque, const uint8_t* pkt, int size);

/**
 * @brief Callback to retransmit PSI/SI tables.
 * @param opaque User data.
 * @param force_pat Force PAT/PMT retransmission.
 * @param force_sdt Force SDT retransmission.
 * @param v_stc Current virtual STC.
 */
typedef void (*tss_si_cb)(void* opaque, int force_pat, int force_sdt, int64_t v_stc);

/**
 * @brief Logging callback.
 */
typedef enum { TSS_LOG_ERROR = 0, TSS_LOG_WARN, TSS_LOG_INFO, TSS_LOG_DEBUG } tss_log_level_t;
typedef void (*tss_log_cb)(void* opaque, tss_log_level_t level, const char* format, ...);

typedef struct {
    uint64_t mux_rate;           /**< Total CBR bitrate (bps) */
    uint32_t pcr_period_ms;      /**< Target PCR interval (default: 35ms) */
    uint32_t mux_delay_ms;       /**< T-STD Target Delay (default: 700ms) */
    uint32_t pcr_window_size;    /**< Metrology window in PCR counts */
    int      debug_level;
    int      nb_streams;         /**< Number of streams in the muxer */

    tss_write_cb write_cb;
    void*        write_opaque;
    tss_si_cb    si_cb;          /**< Callback for PSI/SI retransmission */
    tss_log_cb   log_cb;
    void*        log_opaque;
} tsshaper_config_t;

/**
 * @brief Lifecycle management.
 */
tsshaper_t* tsshaper_create(const tsshaper_config_t* cfg);
void tsshaper_destroy(tsshaper_t* ctx);

/**
 * @brief PID Registration (Analogous to ff_tstd_init loop)
 */
int tsshaper_add_pid(tsshaper_t* ctx, uint16_t pid, tss_pid_type_t type, uint64_t bitrate_bps);

/**
 * @brief Input Path: Push DTS timing info and TS payload.
 */
void tsshaper_enqueue_dts(tsshaper_t* ctx, uint16_t pid, int64_t dts_27m, bool is_key);
void tsshaper_enqueue_ts(tsshaper_t* ctx, uint16_t pid, const uint8_t* pkt_188);

/**
 * @brief Synchronous Flywheel Drive.
 *
 * This drives the Fractional STC and triggers write_cb for every emitted packet.
 */
void tsshaper_drive(tsshaper_t* ctx);

/**
 * @brief Drain the buffers at the end of the session.
 */
void tsshaper_drain(tsshaper_t* ctx);

/**
 * @brief Reset the timeline (Discontinuity handling).
 */
void tsshaper_reset_timeline(tsshaper_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* TSSHAPER_H */
