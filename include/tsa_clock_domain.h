#ifndef TSA_CLOCK_DOMAIN_H
#define TSA_CLOCK_DOMAIN_H

#include <stdint.h>

/**
 * The 4-Clock Domain Model as per Technical Spec V3.1
 * 1. Wall Clock (SystemClock)
 * 2. Arrival Clock (IngressClock)
 * 3. Reference Clock (PCRClock)
 * 4. Media Clock (PTS/DTS)
 */

typedef struct {
    uint64_t last_wall_ns;
    uint64_t last_arrival_ns;
    uint64_t last_pcr_ns;
    uint64_t last_pts_ns;
    
    // Drift state
    double drift_pcr_vs_wall_ppm;
    double drift_pts_vs_pcr_ppm;
} tsa_clock_domain_t;

void tsa_clock_domain_init(tsa_clock_domain_t* cd);

void tsa_clock_domain_update_wall(tsa_clock_domain_t* cd, uint64_t wall_ns);
void tsa_clock_domain_update_arrival(tsa_clock_domain_t* cd, uint64_t arrival_ns);
void tsa_clock_domain_update_pcr(tsa_clock_domain_t* cd, uint64_t pcr_ns);
void tsa_clock_domain_update_pts(tsa_clock_domain_t* cd, uint64_t pts_ns);

#endif
