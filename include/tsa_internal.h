#ifndef TSANALYZER_INTERNAL_H
#define TSANALYZER_INTERNAL_H

#include <stdalign.h>
#include <stdatomic.h>
#include <time.h>

#include "mpmc_queue.h"
#include "tsa.h"
#include "tsa_clock.h"

/* --- Fundamental Types --- */
typedef __int128_t int128_t;
typedef int128_t q64_64;

#define Q_SHIFT 64
#define TO_Q64_64(x) ((q64_64)((x) * (double)((int128_t)1 << 64)))
#define FROM_Q64_64(x) ((double)(x) / (double)((int128_t)1 << 64))
#define INT_TO_Q64_64(x) ((int128_t)(x) << 64)

#define Q32_SHIFT 32
typedef int64_t q32_32;
#define TO_Q32_32(x) ((q32_32)((x)*4294967296.0))
#define FROM_Q32_32(x) ((double)(x) / 4294967296.0)
#define INT_TO_Q32_32(x) ((int64_t)(x) << 32)

#define NS_PER_SEC 1000000000ULL
#define INVALID_PCR 0xFFFFFFFFFFFFFFFFULL

/* --- Protocol Constants --- */
#define TS_AF_FLAG 0x20
#define TS_PAYLOAD_FLAG 0x10
#define TS_PCR_FLAG 0x10

/* --- Helper Structures --- */
typedef struct {
    uint64_t sys_ns;
    uint64_t pcr_ns;
} ts_pcr_sample_t;

typedef struct {
    ts_pcr_sample_t* samples;
    uint32_t size;
    uint32_t count;
    uint32_t head;
} ts_pcr_window_t;

typedef struct {
    const uint8_t* buf;
    int size;
    int pos;
} bit_reader_t;

typedef struct {
    union { char* ptr; char* base; };
    union { size_t size; size_t capacity; };
    size_t offset;
} tsa_metric_buffer_t;

typedef struct {
    uint16_t pid;
    uint8_t pusi;
    uint8_t af_len;
    bool has_payload;
    int payload_len;
    uint8_t cc;
    bool has_discontinuity;
    bool has_pes_header;
    uint64_t pts;
    uint64_t dts;
} ts_decode_result_t;

/* --- Protocol Structures --- */
#define MAX_PROGRAMS 16
#define MAX_STREAMS_PER_PROG 32

typedef struct {
    uint16_t pid;
    uint8_t stream_type;
} ts_stream_info_t;

typedef struct {
    uint16_t program_number;
    uint16_t pmt_pid;
    uint16_t pcr_pid;
    uint32_t stream_count;
    ts_stream_info_t streams[MAX_STREAMS_PER_PROG];
} ts_program_info_t;

typedef enum { TSA_STATUS_VALID = 0, TSA_STATUS_DEGRADED = 1, TSA_STATUS_INVALID = 2 } tsa_measurement_status_t;

typedef struct {
    _Atomic uint64_t total_packets;
    _Atomic uint64_t pid_packet_count[8192];
    _Atomic uint64_t cc_error_count;
    _Atomic uint64_t cc_loss_count;
    _Atomic uint64_t crc_error_count;
    _Atomic uint64_t transport_error_count;
} tsa_sensors_t;

typedef struct {
    uint64_t timestamp_ns;
    uint64_t total_packets;
    uint64_t pid_packet_count[8192];
    uint64_t cc_loss_count;
} tsa_sampling_point_t;

typedef enum { TS_SYNC_HUNTING = 0, TS_SYNC_CONFIRMING, TS_SYNC_LOCKED } tsa_sync_state_t;

typedef enum {
    TSA_EVENT_CC_ERROR = 0,
    TSA_EVENT_PCR_JITTER,
    TSA_EVENT_PCR_REPETITION,
    TSA_EVENT_SCTE35,
    TSA_EVENT_SYNC_LOSS,
    TSA_EVENT_CRC_ERROR,
    TSA_EVENT_PAT_TIMEOUT,
    TSA_EVENT_PMT_TIMEOUT
} tsa_event_type_t;

typedef struct {
    tsa_event_type_t type;
    uint16_t pid;
    uint64_t timestamp_ns;
    uint64_t value;
} tsa_event_t;

#define MAX_EVENT_QUEUE 1024
typedef struct {
    tsa_event_t events[MAX_EVENT_QUEUE];
    alignas(64) _Atomic size_t head;
    alignas(64) _Atomic size_t tail;
} tsa_event_ring_t;

typedef struct {
    uint8_t table_id;
    uint8_t version_number;
    uint8_t last_version;
    bool seen_before;
    uint16_t section_length;
    uint16_t assembled_len;
    uint8_t payload[4096];
    bool active;
    bool complete;
} ts_section_filter_t;

/* --- The Handle --- */
struct tsa_handle {
    tsa_config_t config;

    tsa_sync_state_t sync_state;
    uint32_t sync_confirm_count;
    uint8_t sync_buffer[188 * 2];
    uint32_t sync_buffer_len;

    tsa_sensors_t sensors;
    tsa_sampling_point_t last_sampling_point;
    uint64_t last_pcr_total_pkts;

    ts_section_filter_t* pid_filters;

    alignas(64) uint64_t start_ns;
    bool engine_started;
    uint64_t last_pat_ns;
    uint64_t last_pmt_ns;
    uint64_t last_snap_ns;
    uint64_t last_commit_ns;

    tsa_measurement_status_t* pid_status;
    uint32_t* pid_cc_error_suppression;
    tsa_clock_inspector_t* clock_inspectors;

    bool seen_pat, seen_pmt;
    bool signal_lock;
    int last_trigger_reason;
    uint64_t last_forensic_alarm_count;
    uint32_t consecutive_sync_errors;
    uint32_t consecutive_good_syncs;

    uint64_t last_pcr_ticks;
    uint64_t last_pcr_arrival_ns;
    uint64_t pkts_since_pcr;
    uint64_t last_pcr_interval_bitrate_bps;
    ts_pcr_window_t pcr_window;
    ts_pcr_window_t pcr_long_window;
    uint64_t last_long_pcr_sample_ns;
    double long_term_drift_ppm;
    double stc_wall_drift_ppm;

    tsa_tr101290_stats_t* live;
    tsa_tr101290_stats_t* prev_snap_base;

    char (*pid_labels)[128];
    tsa_srt_stats_t srt_live;

    struct {
        uint64_t dts_ns;
        uint32_t size;
    } (*pid_au_q)[32];
    uint8_t* pid_au_head;
    uint8_t* pid_au_tail;
    uint64_t* pid_pending_dts;

    uint64_t last_v_pts;
    uint64_t last_a_pts;
    uint64_t* pid_last_pts_33;
    uint64_t* pid_pts_offset_64;

    uint64_t metro_last_now;
    uint64_t metro_offset;

    q32_32 pcr_ema_alpha_q32;
    double last_health_score;

    int128_t* pid_eb_fill_q64;
    int128_t* pid_tb_fill_q64;
    int128_t* pid_mb_fill_q64;
    uint64_t* last_buffer_leak_vstc;

    double* pid_bitrate_ema;
    uint64_t* pid_bitrate_min;
    uint64_t* pid_bitrate_max;
    uint8_t* last_cc;
    bool* ignore_next_cc;
    bool* pid_seen;
    bool* pid_is_pmt;
    bool* pid_is_scte35;
    bool* pid_has_cea708;
    uint8_t* pid_stream_type;
    int16_t* pid_to_active_idx;
    uint32_t pid_tracker_count;
    uint16_t pid_active_list[MAX_ACTIVE_PIDS];

    uint32_t program_count;
    ts_program_info_t programs[MAX_PROGRAMS];

    char network_name[256];
    char service_name[256];
    char provider_name[256];

    uint8_t** pid_pes_buf;
    uint32_t* pid_pes_len;
    uint32_t* pid_pes_cap;
    size_t pes_total_allocated;
    size_t pes_max_quota;
    uint32_t pes_pool_used;
    uint16_t* pid_width;
    uint16_t* pid_height;
    uint8_t* pid_profile;
    uint32_t* pid_audio_sample_rate;
    uint8_t* pid_audio_channels;
    uint8_t* pid_log2_max_frame_num;
    uint32_t* pid_last_frame_num;
    bool* pid_frame_num_valid;

    uint32_t* pid_gop_n;
    uint32_t* pid_last_gop_n;
    uint32_t* pid_gop_min;
    uint32_t* pid_gop_max;
    uint64_t* pid_last_idr_ns;
    uint32_t* pid_gop_ms;
    uint64_t* pid_i_frames;
    uint64_t* pid_p_frames;
    uint64_t* pid_b_frames;

    alignas(64) struct {
        _Atomic uint8_t active_idx;
        tsa_snapshot_full_t* buffers[2];
    } double_buffer;

    alignas(64) int128_t stc_slope_q64;
    int128_t stc_intercept_q64;
    bool stc_locked;
    uint64_t stc_ns;
    uint64_t last_pcr_stc_ns;

    uint64_t stc_first_lock_pcr_ns;
    uint64_t stc_first_lock_wall_ns;

    void* pool_base;
    size_t pool_offset;
    size_t pool_size;

    tsa_event_ring_t* event_q;
};

/* --- Internal APIs --- */
void ts_pcr_window_init(ts_pcr_window_t* w, uint32_t sz);
void ts_pcr_window_destroy(ts_pcr_window_t* w);
void ts_pcr_window_add(ts_pcr_window_t* w, uint64_t sys, uint64_t pcr, uint64_t off);
int ts_pcr_window_regress(ts_pcr_window_t* w, double* slope, double* intercept, int64_t* peak_accuracy_ns);

static inline uint32_t read_bits(bit_reader_t* r, int n) {
    uint32_t val = 0;
    for (int i = 0; i < n; i++) {
        if (r->pos / 8 >= r->size) break;
        val = (val << 1) | ((r->buf[r->pos / 8] >> (7 - (r->pos % 8))) & 1);
        r->pos++;
    }
    return val;
}

static inline uint32_t read_ue(bit_reader_t* r) {
    int count = 0;
    while (read_bits(r, 1) == 0 && count < 32) count++;
    if (count >= 32) return 0;
    return (1 << count) - 1 + read_bits(r, count);
}

void tsa_section_filter_push(tsa_handle_t* h, uint16_t pid, const uint8_t* pkt, const ts_decode_result_t* res);
void tsa_decode_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t now_ns, ts_decode_result_t* res);
void tsa_decode_packet_pure(tsa_handle_t* h, const uint8_t* pkt, uint64_t now_ns, ts_decode_result_t* res);
void tsa_metrology_process(tsa_handle_t* h, const uint8_t* pkt, uint64_t now_ns, const ts_decode_result_t* res);

void tsa_precompile_pid_labels(tsa_handle_t* h, uint16_t pid);
void tsa_reset_pid_stats(tsa_handle_t* h, uint16_t pid);
const char* tsa_get_pid_type_name(const tsa_handle_t* h, uint16_t pid);

void tsa_mbuf_init(tsa_metric_buffer_t* b, char* buf, size_t sz);
void tsa_mbuf_append_str(tsa_metric_buffer_t* b, const char* s);
void tsa_mbuf_append_int(tsa_metric_buffer_t* b, int64_t v);
void tsa_mbuf_append_float(tsa_metric_buffer_t* b, float v, int prec);
void tsa_mbuf_append_char(tsa_metric_buffer_t* b, char c);

uint32_t mpegts_crc32(const uint8_t* data, int len);
uint32_t tsa_crc32_check(const uint8_t* data, int len);
uint64_t extract_pcr(const uint8_t* pkt);

typedef enum { TS_CC_OK, TS_CC_DUPLICATE, TS_CC_LOSS, TS_CC_OUT_OF_ORDER } ts_cc_status_t;
ts_cc_status_t cc_classify_error(uint8_t last_cc, uint8_t curr_cc, bool has_payload, bool adaptation_only);

int128_t ts_time_to_ns128(struct timespec ts);
struct timespec ns128_to_timespec(int128_t ns);
int128_t ts_now_ns128(void);

void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* payload, int len, uint64_t now_ns);
void tsa_push_event(tsa_handle_t* h, tsa_event_type_t type, uint16_t pid, uint64_t val);
void tsa_scte35_process(tsa_handle_t* h, uint16_t pid, const uint8_t* payload, int len);

/* --- Test/Utility Helpers --- */
double calculate_shannon_entropy(const uint32_t* counts, int n);
double calculate_pcr_jitter(uint64_t pt, uint64_t pn, double* drift);
const char* tsa_stream_type_to_str(uint8_t type);
float tsa_get_pid_tb_fill(tsa_handle_t* h, uint16_t pid);
float tsa_get_pid_eb_fill(tsa_handle_t* h, uint16_t pid);
float tsa_get_pid_mb_fill(tsa_handle_t* h, uint16_t pid);
int tsa_fast_itoa(char* buf, int64_t val);
int tsa_fast_ftoa(char* buf, float val, int prec);

#endif
