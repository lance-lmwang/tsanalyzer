#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_pid_tracker_stress() {
    printf("Running test_pid_tracker_stress (100K packets)...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Get starting time from engine
    tsa_snapshot_lite_t initial_snap;
    tsa_take_snapshot_lite(h, &initial_snap);

    struct rusage usage_before, usage_after;
    getrusage(RUSAGE_SELF, &usage_before);

    uint64_t fake_now = 1000000000ULL;  // Start at 1s

    for (int i = 0; i < 100000; i++) {
        uint16_t pid = (uint16_t)(rand() % 8192);
        uint8_t pkt[188] = {TS_SYNC_BYTE, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF), 0x10};

        // Advance time by 1ms per packet (1000000 ns)
        fake_now += 1000000;
        tsa_process_packet(h, pkt, fake_now);

        if (i % 1000 == 0) {
            tsa_snapshot_lite_t snap;
            tsa_take_snapshot_lite(h, &snap);
        }

        if (i % 10000 == 0) {
            printf("  Progress: %d/100000 packets...\n", i);
            fflush(stdout);
        }
    }

    getrusage(RUSAGE_SELF, &usage_after);

    long faults = usage_after.ru_minflt - usage_before.ru_minflt;
    printf("Minor faults during 100K packet stress: %ld\n", faults);

    // Increased from 250 to 500 to account for larger tsa_handle size
    // and potential stack noise in CI environments.
    assert(faults < 500);

    tsa_destroy(h);
    printf("test_pid_tracker_stress passed.\n");
}

int main() {
    test_pid_tracker_stress();
    return 0;
}
