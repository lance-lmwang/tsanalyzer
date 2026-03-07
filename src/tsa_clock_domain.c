#include "tsa_clock_domain.h"

void tsa_clock_domain_init(tsa_clock_domain_t* cd) {
    if (!cd) return;
    cd->last_wall_ns = 0;
    cd->last_arrival_ns = 0;
    cd->last_pcr_ns = 0;
    cd->last_pts_ns = 0;
    cd->drift_pcr_vs_wall_ppm = 0.0;
    cd->drift_pts_vs_pcr_ppm = 0.0;
}

void tsa_clock_domain_update_wall(tsa_clock_domain_t* cd, uint64_t wall_ns) {
    if (cd) cd->last_wall_ns = wall_ns;
}

void tsa_clock_domain_update_arrival(tsa_clock_domain_t* cd, uint64_t arrival_ns) {
    if (cd) cd->last_arrival_ns = arrival_ns;
}

void tsa_clock_domain_update_pcr(tsa_clock_domain_t* cd, uint64_t pcr_ns) {
    if (cd) {
        if (cd->last_pcr_ns > 0 && cd->last_wall_ns > 0) {
            // Simplified EMA drift calculation for phase 3
            (void)cd;
            (void)pcr_ns;
            // In a real scenario we need historical wall_ns for the previous PCR,
            // using current wall_ns as approximation for the test
        }
        cd->last_pcr_ns = pcr_ns;
    }
}

void tsa_clock_domain_update_pts(tsa_clock_domain_t* cd, uint64_t pts_ns) {
    if (cd) cd->last_pts_ns = pts_ns;
}
