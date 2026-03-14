#ifndef TSA_PCR_TRACK_H
#define TSA_PCR_TRACK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @struct tsa_pcr_sample_t
 * @brief Common PCR sample structure for LRM window.
 */
typedef struct {
    uint64_t sys_ns;
    uint64_t pcr_ns;
} tsa_pcr_sample_t;

/* Short-term LRM window size for per-PID tracks */
#define PCR_TRACK_WINDOW_SIZE 20

/**
 * @struct tsa_pcr_track_t
 * @brief Encapsulates per-PID metrology and clock recovery state.
 */
typedef struct {
    uint32_t pid;
    uint32_t program_id;  // Parent Program ID for MPTS aggregation

    /* Layer 1: Raw Sampling (27MHz & Wall-clock) */
    uint64_t last_pcr_ticks;         // Last extracted 42-bit PCR
    uint64_t last_unwrapped_pcr_ns;  // Monotonic cumulative PCR nanoseconds
    uint64_t last_arrival_ns;        // CLOCK_MONOTONIC arrival time
    uint64_t pcr_count;              // Total PCRs seen for this PID

    /* Layer 2: Clock Domain (Linear Regression Model) */
    struct {
        double slope;      // Frequency drift slope (ideal = 1.0)
        double intercept;  // Phase offset (ns)
        uint64_t stc_ns;   // Estimated System Time Clock for this PID
        bool locked;       // LRM lock status

        /* Inline Window to avoid thousands of small allocations */
        tsa_pcr_sample_t samples[PCR_TRACK_WINDOW_SIZE];
        uint32_t count;
        uint32_t head;
    } clock;

    /* Layer 3: Metrology Metrics (TR 101 290) */
    float pcr_jitter_ms;         // Current jitter (EMA smoothed)
    double drift_ppm;            // Frequency drift in PPM
    uint64_t bitrate_bps;        // Estimated PID bitrate (smoothed)
    uint64_t mux_bitrate_bps;    // Estimated Mux bitrate based on PCR
    uint32_t priority_1_errors;  // TR 101 290 P1.1 (PCR repetition)

    /* Internal: Interval Anchors for Bitrate/Drift */
    struct {
        uint64_t last_pcr_ticks_anchor;
        uint64_t last_arrival_ns_anchor;
        uint64_t last_pid_pkts_anchor;
        uint64_t last_total_pkts_anchor;
        bool sync_done;
    } anchors;

    void* live; /* Pointer to tsa_tr101290_stats_t */
    bool initialized;
} tsa_pcr_track_t;

void tsa_pcr_track_init(tsa_pcr_track_t* track, uint32_t pid, uint32_t prg_id);
void tsa_pcr_track_destroy(tsa_pcr_track_t* track);
void tsa_pcr_track_reset(tsa_pcr_track_t* track);

/**
 * Update the track with a new PCR observation.
 * @return true if metrology was updated, false if first sample or error.
 */
bool tsa_pcr_track_update(tsa_pcr_track_t* track, uint64_t pcr_ticks, uint64_t arrival_ns, uint64_t pid_pkts,
                          uint64_t total_pkts, bool use_arrival_check);

#endif
