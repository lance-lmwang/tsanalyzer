#ifndef TSA_CLOCK_H
#define TSA_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define PCR_27MHZ_HZ 27000000ULL
#define NS_PER_PCR_TICK 37
#define PCR_TICKS_PER_MS 27000
#define TR101290_P1_1_THRESHOLD_TICKS 1080000 /* 40ms */

typedef enum { TSA_CLOCK_STATE_RESET = 0, TSA_CLOCK_STATE_SYNCING, TSA_CLOCK_STATE_LOCKED } tsa_clock_state_t;

typedef struct {
    uint32_t pid;
    tsa_clock_state_t state;
    bool initialized;

    /* Professional Timing State */
    uint64_t last_pcr_val;
    uint64_t last_pcr_local_ns;

    /* Thresholds & Statistics */
    uint64_t pcr_interval_max_ticks;
    double pcr_jitter_ms;
    uint64_t pcr_count;
    uint32_t priority_1_errors;

    /* Internal Smoothing Filters (Alpha-Beta) */
    double filtered_rate; /* Ticks per Nanosecond */

    /* Error Throttling */
    uint64_t last_error_pcr_val;
    bool pending_discontinuity;

    /* PCR Bitrate Estimation Context (Isolated per clock domain) */
    struct {
        uint64_t last_pcr_ticks;
        uint64_t last_cc_count;
        uint64_t last_bitrate_bps;
        uint64_t last_total_pkts_anchor;
        uint64_t ticks_per_packet_q16;
        double pcr_drift_ppm;
        bool sync_done;
    } br_est;
} tsa_clock_inspector_t;

void tsa_clock_update(const uint8_t *packet, tsa_clock_inspector_t *inspector, uint64_t now_ns);
void tsa_clock_reset(tsa_clock_inspector_t *inspector);

#endif
