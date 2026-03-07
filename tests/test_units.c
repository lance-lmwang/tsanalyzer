#include <assert.h>
#include <stdio.h>

#include "tsa_log.h"
#include "tsa_units.h"

int main() {
    tsa_log_set_level(TSA_LOG_DEBUG);
    tsa_info("units", "Starting unit conversion tests...");

    // Test Bitrate Conversion
    assert(tsa_units_to_bitrate("15Mbps") == 15000000ULL);
    assert(tsa_units_to_bitrate("500kbps") == 500000ULL);
    assert(tsa_units_to_bitrate("1G") == 1000000000ULL);
    tsa_debug("units", "Bitrate conversion passed.");

    // Test Time Conversion (to nanoseconds)
    assert(tsa_units_to_ns("10s") == 10000000000ULL);
    assert(tsa_units_to_ns("500ms") == 500000000ULL);
    assert(tsa_units_to_ns("1min") == 60000000000ULL);
    tsa_debug("units", "Time conversion passed.");

    // Test Boolean
    assert(tsa_units_to_bool("on") == true);
    assert(tsa_units_to_bool("off") == false);
    assert(tsa_units_to_bool("yes") == true);
    tsa_debug("units", "Boolean conversion passed.");

    tsa_info("units", "All unit tests PASSED.");
    return 0;
}
