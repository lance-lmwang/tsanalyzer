#ifndef TSA_TOP_SHM_H
#define TSA_TOP_SHM_H

#include <stdint.h>

#define TSA_TOP_SHM_NAME "/tsa_top_shm"
#define TSA_TOP_MAX_STREAMS 128
#ifndef TSA_ID_MAX
#define TSA_ID_MAX 256
#endif

typedef struct {
    char stream_id[TSA_ID_MAX];

    // Explicitly aligned metric block
    uint64_t total_packets;
    double current_bitrate_mbps;
    double master_health;

    uint64_t cc_errors;
    uint64_t p1_errors;
    uint64_t p2_errors;
    uint64_t p3_errors;

    double pcr_jitter_p99_ms;
    double mdi_df_ms;

    double rst_net_s;
    double rst_enc_s;
    double drift_ppm;
    double drift_long_ppm;

    double width;
    double height;
    double fps;
    double gop_ms;

    uint64_t has_cea708;
    uint64_t has_scte35;
    uint64_t active_alerts_mask;
    uint64_t is_active;
} __attribute__((aligned(64))) tsa_top_stream_info_t;

typedef struct {
    // Avoid _Atomic inside the SHM struct to ensure identical layout across processes
    uint64_t seq_lock;
    uint64_t num_active_streams;
    double global_health;
    tsa_top_stream_info_t streams[TSA_TOP_MAX_STREAMS];
} __attribute__((aligned(64))) tsa_top_shm_block_t;

#endif  // TSA_TOP_SHM_H
