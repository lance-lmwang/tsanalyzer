#ifndef TSS_INTERNAL_H
#define TSS_INTERNAL_H

#include "tsshaper/tsshaper.h"
#include "tss_fifo.h"
#include <stdio.h>
#include <stdarg.h>

#define TSS_SYS_CLOCK_FREQ    27000000LL
#define TSS_TS_PACKET_BITS    (TSS_TS_PACKET_SIZE * 8)

#define TSS_TB_SIZE_STANDARD  512        /* Standard TB_n size in bytes */
#define TSS_RX_RATE_SYS       2000000LL  /* R_x,n for System Data (PSI/SI) = 2.0 Mbps */
#define TSS_RX_RATE_AUDIO     2000000LL  /* R_x,n for Audio Data = 2.0 Mbps */

#define TSS_PSI_FIFO_SIZE     (256 * 1024)
#define TSS_MAX_DRIVE_STEPS   100000
#define TSS_BUFFER_FLOOR_BITS (TSS_TS_PACKET_BITS * 2)

/* T-STD Pacing Control Strategy Constants */
#define TSS_RATIO_TARGET           600  /* 60% buffer fullness */
#define TSS_RATIO_OVER_WARN        1200 /* 120% buffer fullness */
#define TSS_RATIO_OVER_CRITICAL    1600 /* 160% buffer fullness */
#define TSS_RATIO_DANGER_LOW       100  /* 10% buffer fullness */
#define TSS_RATIO_DANGER_HIGH      900  /* 90% buffer fullness */
#define TSS_RATIO_SAFE_LOW         250  /* 25% buffer fullness */
#define TSS_RATIO_SAFE_HIGH        750  /* 75% buffer fullness */
#define TSS_RATIO_OPT_LOW          400  /* 40% buffer fullness */
#define TSS_RATIO_WARN_LOW         200  /* 20% buffer fullness */

#define TSS_RATIO_GENTLE_LOW       TSS_RATIO_DANGER_LOW
#define TSS_RATIO_GENTLE_HIGH      TSS_RATIO_DANGER_HIGH

#define TSS_GAIN_ULTRA_SMOOTH   2
#define TSS_GAIN_GENTLE         4
#define TSS_GAIN_WARNING        8
#define TSS_GAIN_DANGER         16
#define TSS_GAIN_EMERGENCY      24

#define TSS_MAX_BURST_PACKETS   7

#define TSS_SOFT_JUMP_MS        40    /* Maximum smoothing step per packet (ms) */
#define TSS_REANCHOR_TOKEN_MS   50    /* Initial token bucket fill after re-anchor (ms) */

#define TSS_PTS_MODULO          (1LL << 33)
#define TSS_TICKS_PER_PTS       300LL
#define TSS_TIMELINE_MODULO     (TSS_PTS_MODULO * TSS_TICKS_PER_PTS)

#define TSS_JUMP_THRESHOLD_SEC 3
#define TSS_JUMP_THRESHOLD     (TSS_SYS_CLOCK_FREQ * TSS_JUMP_THRESHOLD_SEC)  /* 3 second */
#define TSS_JUMP_WINDOW        (TSS_SYS_CLOCK_FREQ / 2LL)  /* 500ms */
#define TSS_LATE_THRESHOLD     (TSS_SYS_CLOCK_FREQ / 2LL)  /* 500ms */
#define TSS_LATE_MARGIN       (TSS_SYS_CLOCK_FREQ / 10LL) /* 100ms */
#define TSS_DRIFT_THRESHOLD   (TSS_SYS_CLOCK_FREQ / 5LL)  /* 200ms */

#define TSS_REANCHOR_DAMP_NUM 1
#define TSS_REANCHOR_DAMP_DEN 2

#define TSS_PREC_SCALE        1000

#define ACT_NULL         0
#define ACT_PCR_ONLY     1
#define ACT_PAYLOAD      2
#define ACT_PAYLOAD_PCR  3

typedef enum {
    TSS_STATE_NORMAL = 0,
    TSS_STATE_SOFT_DISCONTINUITY = 1,
    TSS_STATE_CONFIRMING = 2,
    TSS_STATE_HARD_DISCONTINUITY = 3,
    TSS_STATE_RECOVERY = 4,
    TSS_STATE_WAIT_IDR = 5
} tss_state_t;

typedef enum {
    JUMP_LEVEL_JITTER,      /* < 50ms */
    JUMP_LEVEL_SOFT,        /* 50ms ~ 500ms */
    JUMP_LEVEL_SUSPICIOUS,  /* 500ms ~ 2s */
    JUMP_LEVEL_HARD         /* > 2s */
} tss_jump_level_t;

typedef struct {
    int64_t last_dts;
    int64_t delta_ms;
    tss_jump_level_t level;
} tss_jump_analyzer_t;

typedef struct {
    int64_t dts;
    int64_t size_bits;
    int is_key;
} tss_access_unit_t;

typedef struct tss_pid_state {
    int pid;
    tss_pid_type_t type;
    tss_fifo_t *ts_fifo;
    int64_t fifo_capacity;
    int64_t vbv_size_bits;

    /* --- Token Bucket --- */
    int64_t tokens_bits;
    int64_t bucket_size_bits;
    int64_t base_refill_rate_bps;
    int64_t effective_refill_rate_bps;
    int64_t refill_rate_base;
    int64_t last_update_ts;
    int64_t token_remainder;

    /* T-STD Decoding Model */
    int64_t bitrate_bps;
    int64_t allocated_cbr_rate;
    int64_t continuous_fullness_bits;
    int64_t drain_remainder;

    /* --- EDF & Timing --- */
    int64_t next_arrival_ts;
    int64_t tb_fullness_bits;
    int64_t tb_size_bits;
    int64_t rx_rate_bps;
    int64_t tb_leak_remainder;
    uint8_t cc;
    int64_t last_dts_raw;
    int64_t buffer_level_bits;
    tss_fifo_t *au_events;
    int au_journal_ingress_offset;

    int64_t last_notified_stc;
    int64_t last_starve_notified_stc;
    int64_t last_overflow_notified_stc;
    int     last_notified_permille;
    int burst_count;
    int needs_discontinuity;
    int need_resync;
    int is_current_au_key;

    /* Jumper & Discontinuity FSM */
    int64_t next_pacing_stc;
    int64_t last_refill_physical_stc;
    int64_t pacing_tokens;
    int64_t pi_integral;

    uint64_t cc_drop_count;
    int     is_panic_mode;
    int     is_urgent;
    int     telemetry_mode;

    int64_t wait_idr_start_stc;
    int64_t resync_count;

    /* Structured Telemetry */
    int64_t tel_audio_shed_count;
    int64_t tel_watchdog_count;
    int64_t tel_psi_rotate_count;
    int64_t tel_pcr_protect_count;

    int64_t tel_max_bps;
    int64_t tel_min_bps;
    int64_t tel_avg_bps_sum;
    int64_t tel_avg_count;
    int64_t tel_input_ema_bps;
    int64_t rx_bytes_total;
    int64_t fifo_accept_bytes;
    int64_t mux_output_bytes;
    int64_t drop_bytes_total;

    int64_t window_start_rx_bytes;
    int64_t window_start_accept_bytes;
    int64_t window_start_emitted_bytes;
    int64_t window_start_drop_bytes;

    int64_t sec_sum_window_in_bps;
    int64_t sec_sum_window_out_bps;
    int64_t sec_sum_window_drop_bps;
    int     sec_sum_window_pkts;
    int     sec_num_windows;

    int64_t last_display_in_bps;
    int64_t last_display_out_bps;
    int64_t last_display_drop_bps;
    int     last_display_out_pkts;

    int64_t last_1s_rx_bytes;
    int64_t last_1s_accept_bytes;
    int64_t last_1s_emitted_bytes;
    int64_t last_1s_drop_bytes;

    int     tel_sec_wait_data;
    int     tel_sec_wait_tok;

    int64_t nominal_refill_bps;
    int64_t tel_sec_slack_min_27m;
    int64_t tel_in_bps_history[5];
    int     tel_in_bps_idx;
    int     tel_in_bps_count;
    int64_t tel_in_bps_max;
    int64_t tel_in_bps_avg;
    int64_t tel_sec_max_au_bits;

    int64_t tel_out_bps_history[5];
    int     tel_out_bps_idx;
    int     tel_out_bps_count;
    int64_t tel_out_bps_max;
    int64_t tel_out_bps_avg;

    int64_t ema_rate_bps;
    int64_t sma_burst_bits;
    int64_t pi_integral_norm;

    tss_jump_analyzer_t analyzer;
    int64_t stream_offset;
    int64_t avg_frame_duration;
    tss_state_t state;
    int confirm_streak;
    int64_t candidate_jump;
    int64_t confirm_start_physical_stc;
} tss_pid_state_t;

typedef struct tss_program {
    int pcr_pid;
    tss_pid_state_t *pcr_pid_state;
    int64_t pcr_period;
    int64_t pcr_packet_period;
    int64_t next_pcr_packet;
    int64_t last_pcr_val;
    int nb_pids;
    tss_pid_state_t **pids;

    int pcr_count;
    int64_t pcr_window_start_pcr;
} tss_program_t;

typedef struct tss_fractional_stc {
    int64_t base;
    int64_t rem;
    int64_t den;
} tss_fractional_stc_t;

struct tsshaper_ctx {
    tsshaper_config_t cfg;

    int     dts_offset_needs_rebuild;
    int     dts_epoch_invalid;
    int64_t v_stc;
    int64_t physical_stc;
    tss_fractional_stc_t stc;
    int64_t stc_offset;
    int64_t mux_delay_27m;
    int64_t max_dts_seen;
    int64_t total_bytes_written;
    int64_t packet_count;

    int64_t ticks_per_packet;
    int64_t rem_per_packet;

    int64_t mux_rate;
    int64_t first_pkt_stc;
    int64_t max_fifo_size;

    int64_t cur_window_bytes;
    int64_t last_window_stc;
    int64_t last_window_dts;

    int nb_programs;
    tss_program_t **programs;
    int nb_all_pids;
    tss_pid_state_t **all_pids;
    tss_pid_state_t *pid_map[8192];
    tss_pid_state_t **stream_index_to_pid;
    tss_pid_state_t *pat_pid_state;
    tss_pid_state_t *sdt_pid_state;
    tss_pid_state_t *last_pid;

    int64_t pat_period;
    int64_t sdt_period;
    int64_t next_pat_ts;
    int64_t next_sdt_ts;
    int64_t last_refill_physical_stc;
    int64_t last_log_stc;

    int64_t first_dts;
    int64_t dts_offset;
    int pending_discontinuity;
    int jump_occurred;
    int in_drive;
    int in_drain;
    int64_t drain_start_vstc;
    int debug_level;
    int pcr_window_size;

    int     tel_trigger_metrology;
    int64_t tel_last_1s_stc;
    int64_t tel_sec_start_stc;
    int64_t tel_sec_null_bytes;
    int64_t tel_sec_pcr_bytes;
    int64_t tel_sec_null_packets;

    int64_t dbg_cnt_payload;
    int64_t dbg_cnt_null;
    int64_t dbg_cnt_pcr;
    int64_t dbg_cnt_si;
    int64_t dbg_null_reason_tb;
    int64_t dbg_null_reason_token;
    int64_t dbg_null_reason_nodata;

    int64_t next_slot_stc;
    int psi_consecutive_count;
};

void tss_log(tsshaper_t *ctx, tss_log_level_t level, const char *format, ...);
tss_pid_state_t* tss_get_state(tsshaper_t *ctx, int stream_index);
tss_pid_state_t* tss_pick_es_pid(tsshaper_t *ctx, tss_program_t **out_prog);
void tss_account_null_packet(tsshaper_t *ctx);
void tss_print_summary(tsshaper_t *ctx, int64_t start_v_stc, int steps);
void tss_refill_tokens_flywheel(tsshaper_t *ctx);
void tss_account_metrology(tsshaper_t *ctx);
int tss_voter_process(tsshaper_t *ctx, tss_pid_state_t *pid, int64_t input_dts_27m, int is_master);
void tss_internal_enqueue_packet(tsshaper_t *ctx, tss_pid_state_t *pid, const uint8_t *packet);

#endif /* TSS_INTERNAL_H */
