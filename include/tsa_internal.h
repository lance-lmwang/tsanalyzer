#ifndef TSANALYZER_INTERNAL_H
#define TSANALYZER_INTERNAL_H

#include <stdatomic.h>
#include <stdalign.h>
#include <time.h>
#include "tsa.h"

typedef __int128_t int128_t;
typedef int128_t q64_64;

#define TO_Q64_64(x) ((q64_64)((x) * (double)((int128_t)1 << 64)))
#define FROM_Q64_64(x) ((double)(x) / (double)((int128_t)1 << 64))

#define NS_PER_SEC 1000000000ULL
#define Q_SHIFT 32

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

/* FIX: Match test case signature q64_64* (int128_t*) slope */
int ts_pcr_window_regress(ts_pcr_window_t* w, int128_t* slope, int128_t* intercept);

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

struct tsa_handle {
    tsa_config_t config;

    alignas(64) uint64_t start_ns;
    uint64_t last_pat_ns;
    uint64_t last_pmt_ns;
    uint64_t last_snap_ns;

    bool seen_pat, seen_pmt;
    bool signal_lock;
    uint32_t consecutive_sync_errors;
    uint32_t consecutive_good_syncs;

    // PCR Analysis Core
    uint64_t last_pcr_ticks;
    uint64_t last_pcr_arrival_ns;
    uint64_t pkts_since_pcr;
    double pcr_jitter_sq_sum_ns;
    uint64_t pcr_jitter_count;
    ts_pcr_window_t pcr_window;

    // Statistical Snapshots
    tsa_tr101290_stats_t live;
    tsa_tr101290_stats_t prev_snap_base;
    tsa_srt_stats_t srt_live;

    /* FIX: Use anonymous union to support both pid_eb_fill_bytes and pid_eb_fill_double */
    union {
        double pid_eb_fill_bytes[TS_PID_MAX];
        double pid_eb_fill_double[TS_PID_MAX];
    };
    uint64_t last_eb_leak_ns[TS_PID_MAX];

    // PID State Tracking
    double pid_bitrate_ema[TS_PID_MAX];
    uint8_t last_cc[TS_PID_MAX];
    bool pid_seen[TS_PID_MAX];
    bool pid_is_pmt[TS_PID_MAX];

    uint32_t program_count;
    ts_program_info_t programs[MAX_PROGRAMS];

    alignas(64) struct {
        _Atomic uint32_t seq;
        tsa_snapshot_full_t stats;
    } snap_state;

    alignas(64) double stc_drift_slope;
    bool stc_locked;
};

/* --- TS Packet Flags --- */
#define TS_AF_FLAG      0x20
#define TS_PAYLOAD_FLAG 0x10
#define TS_PCR_FLAG     0x10

/* --- Internal Utilities --- */
int tsa_fast_itoa(char* buf, int64_t val);
int tsa_fast_ftoa(char* buf, float val, int precision);

/* FIX: Use anonymous union to align engine ptr/size with test base/capacity */
typedef struct {
    union { char* ptr; char* base; };
    union { size_t size; size_t capacity; };
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

void tsa_export_pid_labels(tsa_metric_buffer_t* buf, tsa_handle_t* h, uint16_t pid);

#endif
