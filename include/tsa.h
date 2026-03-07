#ifndef TSANALYZER_H
#define TSANALYZER_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tsp.h"

#define TS_PACKET_SIZE 188
#define TS_PACKET_BITS (TS_PACKET_SIZE * 8)
#define TS_SYNC_BYTE 0x47
#define TS_SYSTEM_CLOCK_HZ 27000000ULL
#define NS_PER_SEC 1000000000ULL
#define TS_MAX_BITRATE_BPS 10000000000ULL
#define DEFAULT_REPLAY_BITRATE 10000000ULL
#define NOMINAL_PACKET_INTERVAL_NS (TS_PACKET_BITS * NS_PER_SEC / DEFAULT_REPLAY_BITRATE)
#define TS_PID_MAX 8192
#define MAX_ACTIVE_PIDS 128
#define TSA_ID_MAX 256
#define TSA_LABEL_MAX 512

typedef struct tsa_handle tsa_handle_t;

typedef struct {
    uint64_t count;
    uint64_t first_timestamp_ns;
    uint64_t last_timestamp_ns;
    uint64_t triggering_vstc;
    uint64_t absolute_byte_offset;
    char message[128];
} tsa_alarm_t;

typedef struct {
    uint64_t bucket_under_1ms;
    uint64_t bucket_1_2ms;
    uint64_t bucket_2_5ms;
    uint64_t bucket_5_10ms;
    uint64_t bucket_10_100ms;
    uint64_t bucket_over_100ms;
} tsa_iat_histogram_t;

typedef struct {
    tsa_alarm_t sync_loss;
    tsa_alarm_t sync_byte_error;
    tsa_alarm_t pat_error;
    tsa_alarm_t cc_error;
    tsa_alarm_t pmt_error;
    tsa_alarm_t pid_error;
    tsa_alarm_t transport_error;
    tsa_alarm_t crc_error;
    tsa_alarm_t pcr_repetition_error;
    tsa_alarm_t pcr_accuracy_error;
    tsa_alarm_t pts_error;
    tsa_alarm_t cat_error;
    tsa_alarm_t sdt_error;
    tsa_alarm_t tstd_underflow;
    tsa_alarm_t tstd_overflow;
    tsa_alarm_t entropy_freeze;
    tsa_alarm_t sdt_timeout;
    tsa_alarm_t nit_timeout;

    bool alarm_sync_loss;
    bool alarm_pat_error;
    bool alarm_cc_error;
    bool alarm_pmt_error;
    bool alarm_pcr_repetition_error;
    bool alarm_pcr_accuracy_error;
    bool alarm_crc_error;
    bool alarm_sdt_error;

    uint64_t cc_loss_count;
    uint64_t cc_duplicate_count;
    uint64_t physical_bitrate_bps;
    uint64_t pcr_bitrate_bps;
    double pcr_jitter_avg_ns;
    uint64_t pcr_jitter_rms_ns;
    uint64_t pcr_jitter_max_ns;
    uint64_t pcr_repetition_max_ms;
    double pcr_accuracy_ns;
    double pcr_accuracy_ns_piecewise;
    double pcr_drift_ppm;
    double mdi_df_ms;
    double mdi_mlr_pkts_s;
    tsa_iat_histogram_t iat_hist;
    double video_fps;
    uint32_t gop_ms;
    int32_t av_sync_ms;
    uint64_t stream_utc_ms;
    uint64_t pid_packet_count[TS_PID_MAX];
    uint64_t pid_bitrate_bps[TS_PID_MAX];
    uint64_t last_pcr_interval_bitrate_bps;
    uint64_t pid_cc_errors[TS_PID_MAX];
    bool pid_is_referenced[TS_PID_MAX];
    uint64_t latched_cc_error;
    float pid_eb_fill_pct[TS_PID_MAX];
    uint32_t pid_eb_fill_bytes[TS_PID_MAX];
    uint64_t pid_last_seen_ns[TS_PID_MAX];
    uint64_t pid_last_seen_vstc[TS_PID_MAX];
    uint64_t total_ts_packets;
    uint64_t engine_processing_latency_ns;
    uint64_t internal_analyzer_drop;
    uint64_t worker_slice_overruns;
} tsa_tr101290_stats_t;

typedef struct tsa_srt_stats {
    int64_t rtt_ms;
    uint32_t byte_rcv_buf;
    uint32_t effective_rcv_latency_ms;
    double retransmit_tax;
    uint64_t bytes_received;
    uint64_t bytes_lost;
} tsa_srt_stats_t;

typedef struct {
    double master_health;
    double rst_network_s;
    double rst_encoder_s;
    double stc_wall_drift_ppm;
    double long_term_drift_ppm;
    double stc_drift_slope;
    uint64_t fault_domain;
    uint64_t lid_active;
    uint64_t stc_locked_bool;
    uint64_t pcr_jitter_ns;
} tsa_predictive_stats_t;

typedef struct {
    uint64_t total_packets;
    uint64_t physical_bitrate_bps;
    bool signal_lock;
    float master_health;
    bool lid_active;
    float rst_network_s;
    float rst_encoder_s;
    uint32_t active_pid_count;
} tsa_snapshot_lite_t;

typedef struct {
    uint32_t pid;
    char type_str[16];
    int64_t bitrate_q16_16;
    uint64_t bitrate_min;
    uint64_t bitrate_max;
    uint64_t cc_errors;
    uint8_t liveness_status;
    uint8_t status;
    uint16_t width;
    uint16_t height;
    uint8_t profile;
    uint8_t level;
    uint8_t chroma_format;
    uint8_t bit_depth;
    float exact_fps;
    uint32_t audio_sample_rate;
    uint8_t audio_channels;
    uint32_t gop_n;
    uint32_t gop_min;
    uint32_t gop_max;
    uint32_t gop_ms;
    uint64_t i_frames;
    uint64_t p_frames;
    uint64_t b_frames;
    uint64_t closed_gops;
    uint64_t open_gops;
    bool has_cea708;
    bool has_scte35;
    bool is_closed_gop;
    int64_t scte35_alignment_error_ns;
    float eb_fill_pct;
    float tb_fill_pct;
    float mb_fill_pct;
} tsa_pid_info_t;

typedef struct {
    tsa_snapshot_lite_t summary;
    tsa_tr101290_stats_t stats;
    tsa_srt_stats_t srt;
    tsa_predictive_stats_t predictive;
    char network_name[256];
    char service_name[256];
    char provider_name[256];
    uint32_t active_pid_count;
    tsa_pid_info_t pids[MAX_ACTIVE_PIDS];
} tsa_snapshot_full_t;

typedef enum {
    TSA_MODE_LIVE = 0,
    TSA_MODE_REPLAY = 1,
    TSA_MODE_FORENSIC = 2,
    TSA_MODE_CERTIFICATION = 3
} tsa_op_mode_t;

typedef enum {
    TSA_FAILOVER_MODE_AUTO,
    TSA_FAILOVER_MODE_FORCE_PRIMARY,
    TSA_FAILOVER_MODE_FORCE_BACKUP,
    TSA_FAILOVER_MODE_MANUAL
} tsa_failover_mode_t;

typedef struct {
    char input_label[TSA_ID_MAX];
    bool is_live;
    tsa_op_mode_t op_mode;

    struct {
        bool enabled;
        bool tr101290;
        bool pcr_jitter;
        bool codec_sniff;
        bool scte35;
        bool entropy;
        double pcr_ema_alpha;
        bool enable_reactive_pid_filter;
    } analysis;

    struct {
        bool enabled;
        char output_url[256];
        uint64_t bitrate;
        tsp_mode_t pacing;
        bool repair_cc;
        bool repair_pcr;
    } gateway;

    struct {
        bool enabled;
        char backup_url[256];
        float threshold;
        uint32_t hold_time_ms;
        tsa_failover_mode_t mode;
    } failover;

    struct {
        char webhook_url[256];
        uint64_t filter_mask;
        uint32_t cooldown_ms;
    } alert;

    // Legacy fields for backward compatibility during transition
    uint64_t forced_cbr_bitrate;
    uint16_t protected_pids[16];
    uint32_t entropy_window_packets;
    int udp_port;
    char url[256];
    char webhook_url[256];
    uint64_t alert_filter_mask;
    int http_port;
} tsa_config_t;

/* --- Core TSA API --- */
tsa_handle_t* tsa_create(const tsa_config_t* cfg);
void tsa_destroy(tsa_handle_t* h);
void tsa_process_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t timestamp_ns);
void tsa_handle_internal_drop(tsa_handle_t* h, uint64_t drop_count);
void tsa_commit_snapshot(tsa_handle_t* h, uint64_t timestamp_ns);
int tsa_take_snapshot_full(tsa_handle_t* h, tsa_snapshot_full_t* s);
int tsa_take_snapshot_lite(tsa_handle_t* h, tsa_snapshot_lite_t* s);
size_t tsa_snapshot_to_json(tsa_handle_t* h, const tsa_snapshot_full_t* snap, char* buf, size_t sz);
void tsa_export_prometheus(tsa_handle_t* h, char* buf, size_t sz);
void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz);
void tsa_exporter_prom_core(tsa_handle_t** handles, int count, char* buf, size_t sz);
void tsa_exporter_prom_pids(tsa_handle_t** handles, int count, char* buf, size_t sz);
void* tsa_mem_pool_alloc(tsa_handle_t* h, size_t size);
void tsa_update_srt_stats(tsa_handle_t* h, const tsa_srt_stats_t* srt);
bool tsa_forensic_trigger(tsa_handle_t* h, int reason);
void tsa_reset_latched_errors(tsa_handle_t* h);
void tsa_forensic_generate_json(tsa_handle_t* h, char* buffer, size_t size);
void tsa_render_dashboard(tsa_handle_t* h);

/* --- Forensic Tools --- */
typedef struct tsa_packet_ring tsa_packet_ring_t;
tsa_packet_ring_t* tsa_packet_ring_create(size_t size);
void tsa_packet_ring_destroy(tsa_packet_ring_t* r);
int tsa_packet_ring_push(tsa_packet_ring_t* r, const uint8_t* pkt, uint64_t timestamp_ns);
int tsa_packet_ring_pop(tsa_packet_ring_t* r, uint8_t* pkt, uint64_t* timestamp_ns);

typedef struct tsa_forensic_writer tsa_forensic_writer_t;
tsa_forensic_writer_t* tsa_forensic_writer_create(tsa_packet_ring_t* ring, const char* filename);
void tsa_forensic_writer_destroy(tsa_forensic_writer_t* w);
int tsa_forensic_writer_write_all(tsa_forensic_writer_t* w);
void tsa_forensic_writer_start(tsa_forensic_writer_t* w);
void tsa_forensic_writer_stop(tsa_forensic_writer_t* w);

/* --- TSA Gateway API --- */
typedef struct tsa_gateway tsa_gateway_t;

typedef struct {
    tsa_config_t analysis_primary;
    tsa_config_t analysis_backup;
    tsp_config_t pacing;

    bool enable_smart_failover;
    float health_threshold;  /* 0.0 - 1.0, e.g. 0.7 triggers switch */
    uint32_t switch_hold_ms; /* Avoid flapping */
    tsa_failover_mode_t failover_mode;

    bool enable_action_engine;
    bool enable_null_substitution;
    bool enable_pcr_restamp;
    bool enable_auto_forensics;
    uint32_t forensic_ring_size;
    bool enable_dynamic_grooming;
    uint64_t watchdog_timeout_ns;
} tsa_gateway_config_t;

tsa_gateway_t* tsa_gateway_create(const tsa_gateway_config_t* cfg);
void tsa_gateway_destroy(tsa_gateway_t* gw);

/**
 * Process a single packet in single-input mode.
 */
int tsa_gateway_process(tsa_gateway_t* gw, const uint8_t* pkt, uint64_t now_ns);

/**
 * Process dual packets in smart-failover mode.
 * @param pkt_p Packet from Primary source (can be NULL if no data)
 * @param pkt_b Packet from Backup source (can be NULL if no data)
 */
int tsa_gateway_process_dual(tsa_gateway_t* gw, const uint8_t* pkt_p, const uint8_t* pkt_b, uint64_t now_ns);

uint32_t tsa_gateway_get_active_index(tsa_gateway_t* gw);
tsa_handle_t* tsa_gateway_get_tsa_handle(tsa_gateway_t* gw);
tsa_handle_t* tsa_gateway_get_tsa_handle_backup(tsa_gateway_t* gw);
struct tsp_handle* tsa_gateway_get_tsp_handle(tsa_gateway_t* gw);
bool tsa_gateway_is_bypassing(tsa_gateway_t* gw);
void tsa_gateway_debug_inject_stall(tsa_gateway_t* gw, uint64_t duration_ns);

/* --- Utility to find a PID in a dense snapshot --- */
static inline int tsa_find_pid_in_snapshot(const tsa_snapshot_full_t* snap, uint16_t pid) {
    for (uint32_t i = 0; i < snap->active_pid_count; i++) {
        if (snap->pids[i].pid == pid) return (int)i;
    }
    return -1;
}

#endif
