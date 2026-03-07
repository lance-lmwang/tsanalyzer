#include <assert.h>
#include <stdio.h>

#include "tsa_clock_domain.h"
#include "tsa_log.h"

int main() {
    tsa_log_set_level(TSA_LOG_DEBUG);
    tsa_info("clock_test", "Starting 4-Clock Domain tests...");

    tsa_clock_domain_t cd;
    tsa_clock_domain_init(&cd);

    tsa_clock_domain_update_wall(&cd, 1000);
    assert(cd.last_wall_ns == 1000);

    tsa_clock_domain_update_arrival(&cd, 2000);
    assert(cd.last_arrival_ns == 2000);

    tsa_clock_domain_update_pcr(&cd, 3000);
    assert(cd.last_pcr_ns == 3000);

    tsa_clock_domain_update_pts(&cd, 4000);
    assert(cd.last_pts_ns == 4000);

    tsa_info("clock_test", "All clock domain tests PASSED.");
    return 0;
}
