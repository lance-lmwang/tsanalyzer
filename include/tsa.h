#ifndef TSANALYZER_H
#define TSANALYZER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TS_PACKET_SIZE 188
#define TS_SYNC_BYTE 0x47
#define TS_PID_MAX 8192

typedef struct tsa_handle tsa_handle_t;

typedef struct {
    uint64_t count;
    uint64_t last_timestamp_ns;
    char message[128];
} tsa_alarm_t;

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

    uint64_t cc_loss_count;
    uint64_t cc_duplicate_count;
    uint64_t physical_bitrate_bps;
    uint64_t pcr_bitrate_bps;
    double pcr_jitter_avg_ns;
    uint64_t pcr_jitter_rms_ns;
    uint64_t pcr_jitter_max_ns;
    uint64_t pcr_repetition_max_ms;
    double pcr_accuracy_ns;
    double pcr_drift_ppm;
    double mdi_df_ms;
    double mdi_mlr_pkts_s;
    double video_fps;
    uint32_t gop_ms;
    int32_t av_sync_ms;
    uint64_t stream_utc_ms;
    uint64_t pid_packet_count[TS_PID_MAX];
    uint64_t pid_bitrate_bps[TS_PID_MAX];
    uint64_t pid_cc_errors[TS_PID_MAX];
    bool pid_is_referenced[TS_PID_MAX];
    uint64_t latched_cc_error;
    float pid_eb_fill_pct[TS_PID_MAX];
    uint32_t pid_eb_fill_bytes[TS_PID_MAX];
    uint64_t pid_last_seen_ns[TS_PID_MAX];
    uint64_t total_ts_packets;
} tsa_tr101290_stats_t;

typedef struct tsa_srt_stats {
    int64_t rtt_ms;
    uint32_t byte_rcv_buf;
    uint32_t effective_rcv_latency_ms;
} tsa_srt_stats_t;

typedef struct {
    float master_health;
    float rst_network_s;
    float rst_encoder_s;
    uint32_t fault_domain;
    bool lid_active;
    double stc_drift_slope;
    bool stc_locked_bool;
} tsa_predictive_stats_t;

typedef struct {
    uint64_t total_packets;
    bool signal_lock;
    float master_health;
    bool lid_active;
    float rst_encoder_s;
    uint32_t active_pid_count;
} tsa_snapshot_lite_t;

typedef struct {
    tsa_snapshot_lite_t summary;
    tsa_tr101290_stats_t stats;
    tsa_srt_stats_t srt;
    tsa_predictive_stats_t predictive;
    struct {
        uint32_t pid;
        char type_str[16];
        int64_t bitrate_q16_16;
        uint64_t cc_errors;
        uint8_t liveness_status;
        uint16_t width;
        uint16_t height;  // Added for es_metadata test
        uint8_t profile;  // Added for es_metadata test
        float eb_fill_pct;
        float tb_fill_pct;
        float mb_fill_pct;
    } pids[TS_PID_MAX];
} tsa_snapshot_full_t;

typedef struct {
    char input_label[32];
    bool is_live;
    double pcr_ema_alpha;
    bool enable_forensics;
    uint64_t forced_cbr_bitrate;
    uint16_t protected_pids[16];
    uint32_t entropy_window_packets; // Added for snapshot test
} tsa_config_t;

/* --- Core TSA API --- */
tsa_handle_t* tsa_create(const tsa_config_t* cfg);
void tsa_destroy(tsa_handle_t* h);
void tsa_process_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t timestamp_ns);
void tsa_commit_snapshot(tsa_handle_t* h, uint64_t timestamp_ns);
int tsa_take_snapshot_full(tsa_handle_t* h, tsa_snapshot_full_t* s);
int tsa_take_snapshot_lite(tsa_handle_t* h, tsa_snapshot_lite_t* s);
size_t tsa_snapshot_to_json(const tsa_snapshot_full_t* snap, char* buf, size_t sz);
void tsa_export_prometheus(tsa_handle_t* h, char* buf, size_t sz);
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
void tsa_forensic_writer_start(tsa_forensic_writer_t* w);
void tsa_forensic_writer_stop(tsa_forensic_writer_t* w);

/* --- TSA Gateway API --- */
typedef struct tsa_gateway tsa_gateway_t;
typedef struct {
    tsa_config_t analysis;
    struct {
        const char* srt_url;
        const char* dest_ip;
        uint16_t port;
        uint64_t bitrate;
        uint32_t ts_per_udp;
        int mode; // tsp_mode_t
    } pacing;
    bool enable_action_engine;
    bool enable_null_substitution;
    bool enable_pcr_restamp;
    uint64_t watchdog_timeout_ns; // Added for fail_safe test
} tsa_gateway_config_t;

tsa_gateway_t* tsa_gateway_create(const tsa_gateway_config_t* cfg);
void tsa_gateway_destroy(tsa_gateway_t* gw);
int tsa_gateway_process(tsa_gateway_t* gw, const uint8_t* pkt, uint64_t now_ns);
tsa_handle_t* tsa_gateway_get_tsa_handle(tsa_gateway_t* gw);
struct tsp_handle* tsa_gateway_get_tsp_handle(tsa_gateway_t* gw);
bool tsa_gateway_is_bypassing(tsa_gateway_t* gw); // Added for fail_safe test
void tsa_gateway_debug_inject_stall(tsa_gateway_t* gw, uint64_t duration_ns); // Added for fail_safe test

#endif
