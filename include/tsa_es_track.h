#ifndef TSA_ES_TRACK_H
#define TSA_ES_TRACK_H

#include <stdbool.h>
#include <stdint.h>

#include "tsa_packet_pool.h"

#define TSA_AU_QUEUE_SIZE 32
#define TSA_PES_MAX_REFS 16384 /* Max TS packets per PES (~3MB capacity) */

/**
 * @struct tsa_es_track_t
 * @brief Consolidated state for Elementary Stream (ES) analysis and T-STD simulation.
 * Optimized for STRICT ZERO-COPY processing.
 */
typedef struct {
    uint16_t pid;
    uint8_t stream_type;
    bool active;
    uint8_t status; /* tsa_measurement_status_t */

    /* Bitrate Statistics */
    double bitrate_ema;
    uint64_t bitrate_min;
    uint64_t bitrate_max;
    uint64_t bitrate_peak;

    /* PES Accumulator State (ZERO-COPY) */
    struct {
        enum { TSA_PES_HUNTING, TSA_PES_ACCUMULATING, TSA_PES_FINISHING } state;

        /* Pointers to packets in global pool instead of local buffer copy */
        tsa_packet_t* refs[TSA_PES_MAX_REFS];
        uint16_t payload_offsets[TSA_PES_MAX_REFS];
        uint8_t payload_lens[TSA_PES_MAX_REFS];
        uint32_t ref_count;

        uint32_t total_length;
        uint64_t last_pts_33;
        uint64_t last_dts_33;
        uint64_t pending_dts_ns;
        bool has_pts;
        bool has_dts;
    } pes;

    /* AU Queue for T-STD Drain Timing */
    struct {
        struct {
            uint64_t dts_ns;
            uint32_t size;
        } queue[TSA_AU_QUEUE_SIZE];
        uint8_t head;
        uint8_t tail;
    } au_q;

    /* Codec Metadata & GOP Analysis */
    struct {
        uint16_t width;
        uint16_t height;
        uint8_t profile;
        uint8_t level;
        uint8_t chroma_format;
        uint8_t bit_depth;
        float exact_fps;

        uint32_t gop_n;
        uint32_t last_gop_n;
        uint32_t gop_min;
        uint32_t gop_max;
        uint32_t gop_ms;
        uint64_t last_idr_ns;

        uint64_t i_frames;
        uint64_t p_frames;
        uint64_t b_frames;
        uint64_t closed_gops;
        uint64_t open_gops;
        bool is_closed_gop;
        bool has_cea708;
    } video;

    struct {
        uint32_t sample_rate;
        uint8_t channels;
    } audio;

    /* SCTE-35 Splicing State */
    struct {
        uint64_t target_pts;
        int64_t alignment_error_ns;
        bool pending_splice;
    } scte35;

    /* T-STD Buffer Simulation (ISO/IEC 13818-1) */
    struct {
        __int128 eb_fill_q64;  // Elementary Buffer fill level (bits)
        __int128 tb_fill_q64;  // Transport Buffer fill level (bits)
        __int128 mb_fill_q64;  // Multiplexing Buffer fill level (bits)
        uint64_t last_leak_vstc;
        uint32_t buffer_size_eb;
        uint32_t buffer_size_tb;
        uint32_t buffer_size_mb;
        uint64_t leak_rate_eb;  // R_eb (bits/s)
        uint64_t leak_rate_rx;  // R_rx (bits/s)
    } tstd;

    /* Time & Continuity tracking */
    uint64_t last_seen_ns;
    uint64_t last_seen_vstc;
    uint64_t last_pts_recorded;
    uint64_t last_pts_extrapolated;
    uint8_t last_cc;
    bool ignore_next_cc;
} tsa_es_track_t;

#endif /* TSA_ES_TRACK_H */
