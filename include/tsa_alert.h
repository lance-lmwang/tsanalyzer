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
    TSA_ALERT_MAX
} tsa_alert_id_t;

typedef enum { TSA_ALERT_STATE_OFF, TSA_ALERT_STATE_FIRING } tsa_alert_status_t;

typedef struct {
    tsa_alert_status_t status;
    uint64_t last_occurrence_ns;
    uint32_t count_in_window;
    bool needs_resolve_msg;
} tsa_alert_state_t;

struct tsa_handle;  // Forward declaration

/**
 * Update the state of a specific alert.
 */
void tsa_alert_update(struct tsa_handle* h, tsa_alert_id_t id, bool has_error, const char* name, uint16_t pid);

/**
 * Periodically called to check for expired (resolved) alerts.
 */
void tsa_alert_check_resolutions(struct tsa_handle* h);

#endif
