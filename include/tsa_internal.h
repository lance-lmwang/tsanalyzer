#ifndef TSANALYZER_INTERNAL_H
#define TSANALYZER_INTERNAL_H

#include <stdalign.h>
#include <stdatomic.h>
#include <time.h>

#include "mpmc_queue.h"
#include "tsa.h"
#include "tsa_alert.h"
#include "tsa_bitstream.h"
#include "tsa_clock.h"
#include "tsa_es_track.h"
#include "tsa_histogram.h"
#include "tsa_log.h"
#include "tsa_pcr_track.h"
#include "tsa_plugin.h"
#include "tsa_stream.h"
#include "tsa_stream_model.h"
#include "tsa_webhook.h"

/* --- Fundamental Types --- */
typedef __int128_t int128_t;
typedef int128_t q64_64;

#define TO_Q64_64(x) ((int128_t)((x) * 18446744073709551616.0))
#define INT_TO_Q64_64(x) ((int128_t)(x) << 64)
#define FROM_Q64_64(x) ((double)(x) / 18446744073709551616.0)

typedef uint64_t q32_32;
#define TO_Q32_32(x) ((uint64_t)((x) * 4294967296.0))

/* --- Global Limits --- */
#define MAX_EVENT_QUEUE 1024
#define MAX_PROGRAMS 16
#define MAX_STREAMS_PER_PROG 32
#define MAX_TSA_PLUGINS 16
#define MAX_PLUGIN_CONTEXT_SIZE 32768

/* --- MPEG-TS Constants --- */
#define TS_AF_FLAG 0x20
#define TS_PAYLOAD_FLAG 0x10
#define TS_PCR_FLAG 0x10

/* TR 101 290 Standard Thresholds (in Nanoseconds) */
#define TSA_TR101290_PAT_TIMEOUT_NS (500000000ULL)   /* 500ms (P1.3) */
#define TSA_TR101290_PMT_TIMEOUT_NS (500000000ULL)   /* 500ms (P1.5) */
#define TSA_TR101290_SDT_TIMEOUT_NS (2000000000ULL)  /* 2s    (P3.2) */
#define TSA_TR101290_NIT_TIMEOUT_NS (10000000000ULL) /* 10s   (P3.1) */
#define TSA_TR101290_PID_TIMEOUT_NS (5000000000ULL)  /* 5s    (P3.x) */

/* --- Enums --- */
typedef enum {
    TS_SYNC_HUNTING,
    TS_SYNC_CONFIRMING,
    TS_SYNC_LOCKED
} tsa_sync_state_t;
typedef enum {
    TSA_STATUS_VALID = 0,
    TSA_STATUS_INVALID = 1,
    TSA_STATUS_DEGRADED = 2
} tsa_measurement_status_t;

typedef enum {
    TSA_EVENT_SCTE35,
    TSA_EVENT_SYNC_LOSS,
    TSA_EVENT_CRC_ERROR,
    TSA_EVENT_PAT_TIMEOUT,
    TSA_EVENT_PMT_TIMEOUT,
    TSA_EVENT_PID_ERROR,
    TSA_EVENT_CC_ERROR,
    TSA_EVENT_TRANSPORT_ERROR,
    TSA_EVENT_PTS_ERROR,
    TSA_EVENT_PCR_REPETITION,
    TSA_EVENT_PCR_JITTER,
    TSA_EVENT_SYNC_RECOVERED,
    TSA_EVENT_TSTD_UNDERFLOW,
    TSA_EVENT_TSTD_OVERFLOW,
    TSA_EVENT_ENTROPY_FREEZE,
    TSA_EVENT_SDT_TIMEOUT,
    TSA_EVENT_NIT_TIMEOUT,
    TSA_EVENT_SCRAMBLED,
    TSA_EVENT_PES_ERROR
} tsa_event_type_t;

typedef enum {
    TS_CC_OK,
    TS_CC_DUPLICATE,
    TS_CC_LOSS,
    TS_CC_OUT_OF_ORDER
} ts_cc_status_t;

typedef struct {
    uint64_t last_occurrence_ns;
    uint64_t last_absence_ns;
    uint64_t fired_time_ns;
    bool is_fired;
} tsa_debounce_t;

/* --- Utility Prototypes --- */
typedef struct {
    char* base;
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

int tsa_fast_itoa(char* buf, int64_t val);
int tsa_fast_ftoa(char* buf, float val, int prec);

int parse_url_ext(const char* url, char* host, int* port, int* is_listener, int* latency, char* passphrase,
                  int* pbkeylen);

typedef struct {
    uint8_t stream_id;
    bool has_pts;
    bool has_dts;
    uint64_t pts;
    uint64_t dts;
    int header_len;
} tsa_pes_header_t;

int tsa_parse_pes_header(const uint8_t* p, int len, tsa_pes_header_t* h);
uint32_t mpegts_crc32(const uint8_t* data, int len);
uint32_t tsa_crc32_check(const uint8_t* data, int len);
ts_cc_status_t cc_classify_error(uint8_t l, uint8_t c, bool p, bool a);

#define TSA_TYPE_VIDEO_MPEG2 0x02
#define TSA_TYPE_VIDEO_H264 0x1b
#define TSA_TYPE_VIDEO_HEVC 0x24
#define TSA_TYPE_AUDIO_AAC 0x0f
#define TSA_TYPE_AUDIO_AC3 0x81
#define TSA_TYPE_PRIVATE 0x06
#define TSA_TYPE_SCTE35 0x86

static inline bool tsa_is_h264(uint8_t type) { return type == TSA_TYPE_VIDEO_H264; }
static inline bool tsa_is_hevc(uint8_t type) { return type == TSA_TYPE_VIDEO_HEVC; }
static inline bool tsa_is_video(uint8_t type) {
    return type == TSA_TYPE_VIDEO_H264 || type == TSA_TYPE_VIDEO_HEVC || type == TSA_TYPE_VIDEO_MPEG2;
}

const char* tsa_stream_type_to_str(uint8_t type);
const char* tsa_get_pid_type_name(const struct tsa_handle* h, uint16_t p);

typedef struct {
    uint16_t pid;
    uint8_t pusi;
    uint8_t af_len;
    bool has_payload;
    int payload_len;
    uint8_t cc;
    bool scrambled;
    bool has_discontinuity;
    bool has_pes_header;
    uint64_t pts;
    uint64_t dts;
} ts_decode_result_t;

typedef struct {
    uint8_t buffer[4096];
    uint32_t len;
    bool active;
    bool complete;
    bool seen_before;
    uint8_t last_ver;
} ts_section_filter_t;
typedef struct {
    uint16_t pid;
    uint8_t stream_type;
} tsa_stream_info_t;
typedef struct {
    uint16_t program_number;
    uint16_t pmt_pid;
    uint16_t pcr_pid;
    uint32_t stream_count;
    tsa_stream_info_t streams[MAX_STREAMS_PER_PROG];
} tsa_program_info_t;

typedef struct {
    tsa_pcr_sample_t* samples;
    uint32_t size;
    uint32_t count;
    uint32_t head;
} ts_pcr_window_t;
void ts_pcr_window_init(ts_pcr_window_t* w, uint32_t s);
void ts_pcr_window_destroy(ts_pcr_window_t* w);
void ts_pcr_window_add(ts_pcr_window_t* w, uint64_t s, uint64_t p, uint64_t o);
int ts_pcr_window_regress(ts_pcr_window_t* w, double* slope, double* intercept, int64_t* max_err);

typedef struct {
    tsa_event_type_t type;
    uint16_t pid;
    uint64_t timestamp_ns;
    uint64_t value;
} tsa_event_t;
typedef struct {
    tsa_event_t events[MAX_EVENT_QUEUE];
    _Atomic size_t head;
    _Atomic size_t tail;
} tsa_event_ring_t;

typedef struct tsa_packet_ring tsa_packet_ring_t;
tsa_packet_ring_t* tsa_packet_ring_create(size_t sz);
void tsa_packet_ring_destroy(tsa_packet_ring_t* r);
int tsa_packet_ring_push(tsa_packet_ring_t* r, const uint8_t* p, uint64_t n);
int tsa_packet_ring_pop(tsa_packet_ring_t* r, uint8_t* p, uint64_t* n);

bool tsa_forensic_trigger(struct tsa_handle* h, int reason);
typedef struct tsa_forensic_writer tsa_forensic_writer_t;
tsa_forensic_writer_t* tsa_forensic_writer_create(tsa_packet_ring_t* r, const char* filename);
void tsa_forensic_writer_destroy(tsa_forensic_writer_t* w);
void tsa_forensic_writer_start(tsa_forensic_writer_t* w);
void tsa_forensic_writer_stop(tsa_forensic_writer_t* w);
int tsa_forensic_writer_write_all(tsa_forensic_writer_t* w);
void tsa_forensic_generate_json(struct tsa_handle* h, char* buf, size_t sz);

struct tsa_handle {
    tsa_config_t config;
    tsa_alert_state_t alerts[TSA_ALERT_MAX];
    tsa_sync_state_t sync_state;
    uint32_t sync_confirm_count;
    uint8_t sync_buffer[TS_PACKET_SIZE];
    size_t sync_buffer_len;
    uint32_t consecutive_sync_errors;
    uint32_t consecutive_good_syncs;
    bool signal_lock;
    bool sync_loss_alert_active;
    bool engine_started;
    uint64_t start_ns;
    uint64_t last_snap_ns;
    uint64_t last_snap_wall_ns;
    uint64_t stc_ns;
    bool stc_locked;
    int128_t stc_slope_q64;
    int128_t stc_intercept_q64;
    uint64_t last_pcr_ticks;
    uint64_t last_pcr_arrival_ns;
    uint64_t last_pcr_interval_bitrate_bps;
    uint16_t master_pcr_pid;
    struct {
        uint64_t pcr_first;
        uint64_t packet_acc;
        uint64_t locked_bitrate_bps;
        uint64_t last_cc_count;
        bool is_measuring;
    } br_calc;
    uint64_t pkts_since_pcr;
    uint64_t pcr_ema_alpha_q32;
    ts_pcr_window_t pcr_window;
    ts_pcr_window_t pcr_long_window;
    uint64_t last_long_pcr_sample_ns;
    double long_term_drift_ppm;
    double stc_wall_drift_ppm;
    tsa_tr101290_stats_t* live;
    tsa_tr101290_stats_t* prev_snap_base;
    struct {
        tsa_snapshot_full_t* buffers[2];
        _Atomic uint8_t active_idx;
    } double_buffer;
    bool pending_snapshot;
    uint64_t snapshot_stc;
    float last_health_score;
    bool* pid_seen;
    bool* pid_is_pmt;
    bool* pid_is_scte35;
    uint32_t* pid_cc_error_suppression;
    tsa_pcr_track_t* pcr_tracks;
    tsa_es_track_t* es_tracks;
    uint64_t* prev_snap_base_frames;
    char (*pid_labels)[TSA_LABEL_MAX];
    uint64_t last_v_pts;
    uint64_t last_a_pts;
    uint64_t last_packet_rx_ns;
    struct {
        uint64_t last_snap_bytes;
        uint64_t last_bps;
        uint64_t window_start_ns;
    } phys_stats;
    ts_section_filter_t* pid_filters;
    uint32_t program_count;
    tsa_program_info_t programs[MAX_PROGRAMS];
    tsa_ts_model_t ts_model;
    char network_name[256];
    char service_name[256];
    char provider_name[256];
    bool seen_pat, seen_pmt;
    uint64_t last_pat_ns, last_pmt_ns, last_sdt_ns, last_nit_ns;
    uint16_t pid_active_list[MAX_ACTIVE_PIDS];
    int16_t pid_to_active_idx[TS_PID_MAX];
    uint32_t pid_tracker_count;
    tsa_op_mode_t op_mode;
    tsa_srt_stats_t srt_live;

    tsa_packet_pool_t* pkt_pool;
    uint32_t pes_pool_used;

    void* pool_base;
    size_t pool_offset;
    size_t pool_size;

    uint64_t pes_total_allocated;
    uint64_t pes_max_quota;
    int last_trigger_reason;
    uint64_t last_forensic_alarm_count;
    tsa_event_ring_t* event_q;
    tsa_webhook_engine_t* webhook;
    tsa_debounce_t debounce_cc;
    tsa_debounce_t debounce_transport;
    tsa_debounce_t debounce_pts;
    tsa_stream_t root_stream;
    uint64_t current_ns;
    ts_decode_result_t current_res;
    struct {
        void* instance;
        tsa_plugin_ops_t* ops;
        bool in_use;
        alignas(16) uint8_t context[MAX_PLUGIN_CONTEXT_SIZE];
    } plugins[MAX_TSA_PLUGINS];
    int plugin_count;
    tsa_histogram_t* pid_histograms[TS_PID_MAX];
};

int128_t ts_now_ns128(void);
int128_t ts_time_to_ns128(struct timespec ts);
struct timespec ns128_to_timespec(int128_t ns);
void tsa_tstd_drain(struct tsa_handle* h, uint16_t pid);
int16_t tsa_update_pid_tracker(struct tsa_handle* h, uint16_t pid);
void tsa_reset_pid_stats(struct tsa_handle* h, uint16_t pid);
void tsa_precompile_pid_labels(struct tsa_handle* h, uint16_t pid);
void tsa_clock_update(const uint8_t* packet, tsa_clock_inspector_t* inspector, uint64_t now_ns, bool is_live);
void tsa_section_filter_push(struct tsa_handle* h, uint16_t pid, const uint8_t* pkt, const ts_decode_result_t* res);
void tsa_push_event(struct tsa_handle* h, tsa_event_type_t type, uint16_t pid, uint64_t val);
void tsa_handle_es_payload(struct tsa_handle* h, uint16_t pid, const uint8_t* buf, int len, uint64_t now_ns);
float tsa_get_pid_tb_fill(struct tsa_handle* h, uint16_t pid);
float tsa_get_pid_mb_fill(struct tsa_handle* h, uint16_t pid);
float tsa_get_pid_eb_fill(struct tsa_handle* h, uint16_t pid);
void tsa_decode_packet(struct tsa_handle* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r);
void tsa_decode_packet_pure(struct tsa_handle* h, const uint8_t* p, uint64_t n, ts_decode_result_t* r);
double calculate_shannon_entropy(const uint32_t* counts, int len);
double calculate_pcr_jitter(uint64_t pcr, uint64_t now, double* drift);
void tsa_scte35_process(struct tsa_handle* h, uint16_t pid, const uint8_t* data, int len);
float tsa_calculate_health(struct tsa_handle* h);
void tsa_descriptors_init(void);

#endif  // TSANALYZER_INTERNAL_H
