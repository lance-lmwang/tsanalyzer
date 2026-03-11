#ifndef TSA_ALERT_H
#define TSA_ALERT_H

#include <stdbool.h>
#include <stdint.h>

/* TR 101 290 Standard Alert Masks */
#define TSA_ALERT_MASK_P1_1_SYNC (1ULL << 0)
#define TSA_ALERT_MASK_P1_3_PAT (1ULL << 1)
#define TSA_ALERT_MASK_P1_4_CC (1ULL << 2)
#define TSA_ALERT_MASK_P1_5_PMT (1ULL << 3)
#define TSA_ALERT_MASK_P1_6_PID (1ULL << 4)
#define TSA_ALERT_MASK_P2_1_TRANSPORT (1ULL << 5)
#define TSA_ALERT_MASK_P2_2_CRC (1ULL << 6)
#define TSA_ALERT_MASK_P2_3_PCR (1ULL << 7)
#define TSA_ALERT_MASK_P2_5_PTS (1ULL << 8)
#define TSA_ALERT_MASK_TSTD (1ULL << 9)
#define TSA_ALERT_MASK_ENTROPY (1ULL << 10)
#define TSA_ALERT_MASK_SDT (1ULL << 11)
#define TSA_ALERT_MASK_NIT (1ULL << 12)
#define TSA_ALERT_MASK_CC_MISSING (1ULL << 13)

#define TSA_ALERT_MASK_ALL_P1                                                                                \
    (TSA_ALERT_MASK_P1_1_SYNC | TSA_ALERT_MASK_P1_3_PAT | TSA_ALERT_MASK_P1_4_CC | TSA_ALERT_MASK_P1_5_PMT | \
     TSA_ALERT_MASK_P1_6_PID)
#define TSA_ALERT_MASK_ALL (0xFFFFFFFFFFFFFFFFULL)

typedef enum {
    TSA_ALERT_SYNC,
    TSA_ALERT_PAT,
    TSA_ALERT_PMT,
    TSA_ALERT_PID,
    TSA_ALERT_CC,
    TSA_ALERT_CRC,
    TSA_ALERT_PCR,
    TSA_ALERT_TRANSPORT,
    TSA_ALERT_PTS,
    TSA_ALERT_TSTD,
    TSA_ALERT_ENTROPY,
    TSA_ALERT_SDT,
    TSA_ALERT_NIT,
    TSA_ALERT_CC_MISSING,
    TSA_ALERT_MAX
} tsa_alert_id_t;

typedef enum { TSA_ALERT_STATE_OFF, TSA_ALERT_STATE_FIRING } tsa_alert_status_t;

typedef struct {
    tsa_alert_status_t status;
    uint64_t last_occurrence_ns;
    uint32_t count_in_window;
    bool needs_resolve_msg;
} tsa_alert_state_t;

#define TSA_ALERT_AGGREGATOR_SIZE 4096

typedef struct {
    uint32_t hash;
    tsa_alert_id_t id;
    uint16_t pid;
    _Atomic uint32_t hit_count;
    _Atomic uint64_t last_hit_ns;
    _Atomic uint64_t window_start_ns;
    char stream_id[64];
    char alert_name[32];
    char message[256];
    bool active;
} tsa_alert_aggregator_entry_t;

typedef struct {
    tsa_alert_aggregator_entry_t entries[TSA_ALERT_AGGREGATOR_SIZE];
    uint32_t window_ms;
} tsa_alert_aggregator_t;

struct tsa_handle;  // Forward declaration

/**
 * Get the bitmask for a specific alert ID.
 */
static inline uint64_t tsa_alert_get_mask(tsa_alert_id_t id) {
    switch (id) {
        case TSA_ALERT_SYNC:
            return TSA_ALERT_MASK_P1_1_SYNC;
        case TSA_ALERT_PAT:
            return TSA_ALERT_MASK_P1_3_PAT;
        case TSA_ALERT_PMT:
            return TSA_ALERT_MASK_P1_5_PMT;
        case TSA_ALERT_PID:
            return TSA_ALERT_MASK_P1_6_PID;
        case TSA_ALERT_CC:
            return TSA_ALERT_MASK_P1_4_CC;
        case TSA_ALERT_TRANSPORT:
            return TSA_ALERT_MASK_P2_1_TRANSPORT;
        case TSA_ALERT_CRC:
            return TSA_ALERT_MASK_P2_2_CRC;
        case TSA_ALERT_PCR:
            return TSA_ALERT_MASK_P2_3_PCR;
        case TSA_ALERT_PTS:
            return TSA_ALERT_MASK_P2_5_PTS;
        case TSA_ALERT_TSTD:
            return TSA_ALERT_MASK_TSTD;
        case TSA_ALERT_ENTROPY:
            return TSA_ALERT_MASK_ENTROPY;
        case TSA_ALERT_SDT:
            return TSA_ALERT_MASK_SDT;
        case TSA_ALERT_NIT:
            return TSA_ALERT_MASK_NIT;
        case TSA_ALERT_CC_MISSING:
            return TSA_ALERT_MASK_CC_MISSING;
        default:
            return 0;
    }
}

/**
 * Get the string name for a specific alert ID.
 */
static inline const char* tsa_alert_get_name(tsa_alert_id_t id) {
    switch (id) {
        case TSA_ALERT_SYNC:
            return "SYNC";
        case TSA_ALERT_PAT:
            return "PAT";
        case TSA_ALERT_PMT:
            return "PMT";
        case TSA_ALERT_PID:
            return "PID";
        case TSA_ALERT_CC:
            return "CC";
        case TSA_ALERT_TRANSPORT:
            return "TRANSPORT";
        case TSA_ALERT_CRC:
            return "CRC";
        case TSA_ALERT_PCR:
            return "PCR";
        case TSA_ALERT_PTS:
            return "PTS";
        case TSA_ALERT_TSTD:
            return "TSTD";
        case TSA_ALERT_ENTROPY:
            return "ENTROPY";
        case TSA_ALERT_SDT:
            return "SDT";
        case TSA_ALERT_NIT:
            return "NIT";
        case TSA_ALERT_CC_MISSING:
            return "CC_MISSING";
        default:
            return "UNKNOWN";
    }
}

/**
 * Update the state of a specific alert.
 */
void tsa_alert_update(struct tsa_handle* h, tsa_alert_id_t id, bool has_error, const char* name, uint16_t pid);

/**
 * Periodically called to check for expired (resolved) alerts.
 */
void tsa_alert_check_resolutions(struct tsa_handle* h);

#endif
