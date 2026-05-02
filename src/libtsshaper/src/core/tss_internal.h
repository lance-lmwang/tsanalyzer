#ifndef TSS_INTERNAL_H
#define TSS_INTERNAL_H

#include "tsshaper/tsshaper.h"
#include "tss_fifo.h"
#include <stdio.h>
#include <stdarg.h>

#define TSS_SYS_CLOCK_FREQ    27000000LL
#define TSS_TS_PACKET_BITS    (TSS_TS_PACKET_SIZE * 8)

/* Standard T-STD Constants */
#define TSS_TB_SIZE_STANDARD  512
#define TSS_RX_RATE_SYS       2000000LL
#define TSS_RX_RATE_AUDIO     2000000LL
#define TSS_PSI_FIFO_SIZE     (128 * 1024)
#define TSS_BUFFER_FLOOR_BITS (128 * 1024 * 8)

/* PI Control Ratios (Scaled by 1000) */
#define TSS_RATIO_TARGET         900
#define TSS_RATIO_SAFE_LOW       700
#define TSS_RATIO_SAFE_HIGH      1100
#define TSS_RATIO_OPT_LOW        600
#define TSS_RATIO_GENTLE_LOW     500
#define TSS_RATIO_GENTLE_HIGH    1300
#define TSS_RATIO_WARN_LOW       300
#define TSS_RATIO_DANGER_LOW     100
#define TSS_RATIO_DANGER_HIGH    1500
#define TSS_RATIO_OVER_WARN      1800
#define TSS_RATIO_OVER_CRITICAL  2500

/* PI Control Gains (Scaled by 1000) */
#define TSS_GAIN_ULTRA_SMOOTH    1
#define TSS_GAIN_GENTLE          5
#define TSS_GAIN_WARNING         20
#define TSS_GAIN_EMERGENCY       50

#define TSS_PREC_SCALE           1000
#define TSS_MAX_DRIVE_STEPS      1000000

typedef enum {
    TSS_STATE_NORMAL = 0,
    TSS_STATE_CONFIRMING,
    TSS_STATE_HARD_DISCONTINUITY,
    TSS_STATE_WAIT_IDR
} tss_state_t;

typedef struct {
    int64_t dts;
    int64_t size_bits;
    int is_key;
} tss_access_unit_t;

typedef struct tss_pid_state {
    uint16_t pid;
    tss_pid_type_t type;
    tss_fifo_t *ts_fifo;
    int64_t fifo_capacity;
    int64_t vbv_size_bits;

    /* Token Bucket */
    int64_t tokens_bits;
    int64_t bucket_size_bits;
    int64_t base_refill_rate_bps;
    int64_t effective_refill_rate_bps;
    int64_t last_update_ts;
    int64_t token_remainder;

    /* T-STD Model */
    int64_t bitrate_bps;
    int64_t allocated_cbr_rate;
    int64_t continuous_fullness_bits;
    int64_t tb_fullness_bits;
    int64_t tb_size_bits;
    int64_t rx_rate_bps;
    int64_t tb_leak_remainder;

    /* EDF & Timing */
    int64_t next_arrival_ts;
    uint8_t cc;
    int64_t last_dts_raw;
    int64_t buffer_level_bits;
    tss_fifo_t *au_events;
    int64_t au_bits_acc;

    /* Pacing Control */
    int64_t pacing_tokens;
    int64_t pi_integral;
    int64_t nominal_refill_bps;
    int64_t tel_input_ema_bps;

    /* Telemetry & Statistics (Ported from tstd_metrics.c) */
    uint64_t last_1s_emitted_bytes;
    uint64_t last_1s_accept_bytes;
    uint64_t last_1s_drop_bytes;
    int64_t last_display_out_bps;
    int64_t last_display_in_bps;
    int64_t last_display_drop_bps;
    int last_display_out_pkts;

    int64_t tel_in_bps_history[5];
    int64_t tel_out_bps_history[5];
    int tel_in_bps_idx;
    int tel_out_bps_idx;
    int tel_in_bps_count;
    int tel_out_bps_count;
    int64_t tel_in_bps_avg;
    int64_t tel_in_bps_max;
    int64_t tel_out_bps_avg;
    int64_t tel_out_bps_max;

    int64_t tel_sec_max_au_bits;
    int64_t resync_count;

    /* Aggregated window stats */
    int64_t sec_sum_window_out_bps;
    int64_t sec_sum_window_in_bps;
    int64_t sec_sum_window_drop_bps;
    int64_t sec_sum_window_pkts;
    int sec_num_windows;

    /* State & Telemetry */
    tss_state_t state;
    bool is_urgent;
    bool is_panic_mode;
    int telemetry_mode;
    int burst_count;

    /* Statistics */
    uint64_t mux_output_bytes;
    uint64_t rx_bytes_total;
    uint64_t fifo_accept_bytes;
    uint64_t drop_bytes_total;

    /* Window Statistics */
    uint64_t window_start_emitted_bytes;
    uint64_t window_start_rx_bytes;
    uint64_t window_start_accept_bytes;
    uint64_t window_start_drop_bytes;

    int64_t tel_sec_slack_min_27m;
    int tel_sec_wait_data;
    int tel_sec_wait_tok;
    int tel_avg_count;
} tss_pid_state_t;

struct tsshaper_ctx {
    tsshaper_config_t cfg;

    /* Timing */
    int64_t v_stc;
    int64_t physical_stc;
    int64_t stc_offset;
    struct {
        int64_t base;
        int64_t rem;
        int64_t den;
    } stc;
    int64_t ticks_per_packet;
    int64_t rem_per_packet;
    int64_t max_dts_seen;
    int64_t dts_offset;
    bool dts_epoch_invalid;

    /* PID Management */
    tss_pid_state_t *pid_map[8192];
    tss_pid_state_t **all_pids;
    int nb_all_pids;
    tss_pid_state_t *last_pid;

    /* Internal State */
    bool in_drive;
    bool in_drain;
    bool pending_discontinuity;
    bool jump_occurred;
    int64_t next_slot_stc;
    int64_t last_refill_physical_stc;
    int64_t total_bytes_written;
    uint64_t packet_count;

    /* Telemetry State */
    int64_t tel_last_1s_stc;
    int64_t tel_sec_null_packets;
    int64_t dbg_cnt_null;
    int64_t dbg_cnt_payload;
    int64_t dbg_cnt_pcr;
    int64_t dbg_cnt_si;
    int64_t dbg_null_reason_nodata;
    int64_t dbg_null_reason_tb;
    int64_t dbg_null_reason_token;

    /* SI Scheduling */
    int64_t next_pat_ts;
    int64_t next_sdt_ts;
    int psi_consecutive_count;
};

/* Internal Prototypes */
void tss_log(tsshaper_t *ctx, tss_log_level_t level, const char *format, ...);
tss_pid_state_t* tss_find_pid(tsshaper_t *ctx, uint16_t pid);
void tss_refill_tokens_flywheel(tsshaper_t *ctx);
tss_pid_state_t* tss_pick_es_pid(tsshaper_t *ctx);
void tss_account_null_packet(tsshaper_t *ctx);
void tss_account_metrology(tsshaper_t *ctx);

#endif
