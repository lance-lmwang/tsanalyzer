#ifndef TSANALYZER_INTERNAL_H
#define TSANALYZER_INTERNAL_H

#include <stdalign.h>
#include <stdatomic.h>
#include <time.h>

#include "tsa.h"

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

typedef uint64_t stc_27m_t;

typedef struct {
    stc_27m_t dts;
    stc_27m_t pts;
    size_t size;
    uint64_t arrival_vstc;
    uint16_t pid;
} ts_access_unit_t;

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

void ts_pcr_window_init(ts_pcr_window_t* w, uint32_t sz);
void ts_pcr_window_destroy(ts_pcr_window_t* w);
void ts_pcr_window_add(ts_pcr_window_t* w, uint64_t sys, uint64_t pcr, uint64_t off);

/* Updated signature for determinism: use int64_t for peak_acc in ns */
int ts_pcr_window_regress(ts_pcr_window_t* w, int128_t* slope, int128_t* intercept, int64_t* peak_accuracy_ns);

#define MAX_PROGRAMS 16
#define MAX_STREAMS_PER_PROG 32

typedef struct {
    uint16_t pid;
    uint8_t stream_type;
} ts_stream_info_t;

typedef struct {
    uint16_t pmt_pid;
    uint16_t pcr_pid;
    uint32_t stream_count;
    ts_stream_info_t streams[MAX_STREAMS_PER_PROG];
} ts_program_info_t;

typedef enum { TSA_STATUS_VALID = 0, TSA_STATUS_DEGRADED = 1, TSA_STATUS_INVALID = 2 } tsa_measurement_status_t;

struct tsa_handle {
    tsa_config_t config;

    alignas(64) uint64_t start_ns;
    bool engine_started;
    uint64_t last_pat_ns;
    uint64_t last_pmt_ns;
    uint64_t last_snap_ns;

    tsa_measurement_status_t* pid_status;  // Dynamic [TS_PID_MAX]

    bool seen_pat, seen_pmt;
    bool signal_lock;
    int last_trigger_reason;
    uint64_t last_forensic_alarm_count;
    uint32_t consecutive_sync_errors;
    uint32_t consecutive_good_syncs;

    // PCR Analysis Core
    uint64_t last_pcr_ticks;
    uint64_t last_pcr_arrival_ns;
    uint64_t pkts_since_pcr;
    int128_t pcr_jitter_sq_sum_ns;
    uint64_t pcr_jitter_count;
    ts_pcr_window_t pcr_window;

    // Statistical Snapshots (Now Pointers)
    tsa_tr101290_stats_t* live;
    tsa_tr101290_stats_t* prev_snap_base;
    tsa_srt_stats_t srt_live;

    q32_32 pcr_ema_alpha_q32;
    float last_health_score;

    /* Fixed-point Buffer Simulation (Q64.64) - Dynamic */
    int128_t* pid_eb_fill_q64;
    int128_t* pid_tb_fill_q64;
    int128_t* pid_mb_fill_q64;
    uint64_t* last_buffer_leak_vstc;

    // PID State Tracking - Dynamic
    double* pid_bitrate_ema;
    uint64_t* pid_bitrate_min;
    uint64_t* pid_bitrate_max;
    uint8_t* last_cc;
    bool* pid_seen;
    bool* pid_is_pmt;
    uint8_t* pid_stream_type;
    int16_t* pid_to_active_idx;
    uint32_t pid_tracker_count;
    uint16_t pid_active_list[MAX_ACTIVE_PIDS];  // Small, keep in-struct

    uint32_t program_count;
    ts_program_info_t programs[MAX_PROGRAMS];

    // PES & ES Deep Analysis (Expert Mode) - Dynamic
    uint8_t** pid_pes_buf;
    uint32_t* pid_pes_len;
    uint32_t* pid_pes_cap;
    size_t pes_total_allocated;
    size_t pes_max_quota;
    uint16_t* pid_width;
    uint16_t* pid_height;
    uint8_t* pid_profile;
    uint32_t* pid_audio_sample_rate;
    uint8_t* pid_audio_channels;
    uint8_t* pid_log2_max_frame_num;
    uint32_t* pid_last_frame_num;
    bool* pid_frame_num_valid;

    // GOP Tracking - Dynamic
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
        _Atomic uint32_t seq;
        tsa_snapshot_full_t* stats;  // Now a pointer to avoid MB-sized copy risks
    } snap_state;

    alignas(64) int128_t stc_slope_q64;
    int128_t stc_intercept_q64;
    bool stc_locked;
    uint64_t stc_ns;           // PCR-driven System Time Clock
    uint64_t last_pcr_stc_ns;  // STC at last PCR arrival

    void* pool_base;
    size_t pool_offset;
    size_t pool_size;
};

/* Stage-specific processing for multithreaded pipeline */
typedef struct {
    uint16_t pid;
    uint8_t pusi;
    uint8_t af_len;
    bool has_payload;
    int payload_len;
    uint8_t cc;
    bool has_discontinuity;
} ts_decode_result_t;

void tsa_decode_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t now_ns, ts_decode_result_t* res);
void tsa_decode_packet_pure(tsa_handle_t* h, const uint8_t* pkt, uint64_t now_ns, ts_decode_result_t* res);
void tsa_metrology_process(tsa_handle_t* h, const uint8_t* pkt, uint64_t now_ns, const ts_decode_result_t* res);

/* --- TS Packet Flags --- */
#define TS_AF_FLAG 0x20
#define TS_PAYLOAD_FLAG 0x10
#define TS_PCR_FLAG 0x10

/* --- Internal Utilities --- */
const char* tsa_get_pid_type_name(const tsa_handle_t* h, uint16_t pid);
int tsa_fast_itoa(char* buf, int64_t val);
int tsa_fast_ftoa(char* buf, float val, int precision);

/* FIX: Use anonymous union to align engine ptr/size with test base/capacity */
typedef struct {
    union {
        char* ptr;
        char* base;
    };
    union {
        size_t size;
        size_t capacity;
    };
    size_t offset;
} tsa_metric_buffer_t;

void tsa_mbuf_init(tsa_metric_buffer_t* b, char* buf, size_t sz);
void tsa_mbuf_append_str(tsa_metric_buffer_t* b, const char* s);
void tsa_mbuf_append_int(tsa_metric_buffer_t* b, int64_t v);
void tsa_mbuf_append_float(tsa_metric_buffer_t* b, float v, int prec);
void tsa_mbuf_append_char(tsa_metric_buffer_t* b, char c);

uint32_t mpegts_crc32(const uint8_t* data, int len);

/* FIX: Add missing T-STD function declarations for tests */
float tsa_get_pid_tb_fill(tsa_handle_t* h, uint16_t pid);
float tsa_get_pid_mb_fill(tsa_handle_t* h, uint16_t pid);
float tsa_get_pid_eb_fill(tsa_handle_t* h, uint16_t pid);

double calculate_shannon_entropy(const uint32_t* counts, int len);
int check_cc_error(uint8_t last_cc, uint8_t curr_cc, bool has_payload, bool adaptation_only);
double calculate_pcr_jitter(uint64_t pcr, uint64_t now, double* drift);
uint64_t extract_pcr(const uint8_t* pkt);

typedef enum { TS_CC_OK, TS_CC_DUPLICATE, TS_CC_LOSS, TS_CC_OUT_OF_ORDER } ts_cc_status_t;
ts_cc_status_t cc_classify_error(uint8_t last_cc, uint8_t curr_cc, bool has_payload, bool adaptation_only);

int128_t ts_time_to_ns128(struct timespec ts);
struct timespec ns128_to_timespec(int128_t ns);
int128_t ts_now_ns128(void);

void tsa_handle_es_payload(tsa_handle_t* h, uint16_t pid, const uint8_t* payload, int len, uint64_t now_ns);
const char* tsa_stream_type_to_str(uint8_t type);
void tsa_export_pid_labels(tsa_metric_buffer_t* buf, tsa_handle_t* h, uint16_t pid);

#endif
