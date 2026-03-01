#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_eb_underflow_detection() {
    printf("Running test_eb_underflow_detection...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188] = {TS_SYNC_BYTE, 0x01, 0x00, 0x10};  // PID 0x100
    h->live->pid_is_referenced[0x100] = true;             // Mark as elementary stream

    // Simulate 100kbps stream (very low)
    // Required ES rate is ~40Mbps (based on my 50Mbps default TS rate in implementation)
    // Leak at ES rate: 50 * 0.8 = 40 Mbps = 5 MB/s = 5 bytes/us.
    // 1 packet every 15ms = 188 / 0.015 = 12533 B/s = 100kbps.

    uint64_t start_ns = 1000000000ULL;
    for (int i = 0; i < 200; i++) {
        tsa_process_packet(h, pkt, start_ns + (uint64_t)i * 15000000ULL);  // 15ms gap
    }

    tsa_commit_snapshot(h, start_ns + 3000000000ULL);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    int idx = tsa_find_pid_in_snapshot(&snap, 0x100);
    assert(idx != -1);

    printf("EB Fill: %u bytes, Pct: %.2f%%\n", snap.stats.pid_eb_fill_bytes[0x100], snap.pids[idx].eb_fill_pct);

    // RED PHASE: We expect EB to be empty (0 bytes) because drain > input.
    // We also expect some indication of underflow.
    assert(snap.stats.pid_eb_fill_bytes[0x100] == 0);

    // In Task 2.4, I implemented RST for OVERFLOW.
    // For UNDERFLOW, RST should also be 0 if already underflowing.
    printf("RST Encoder: %.2f s\n", snap.summary.rst_encoder_s);

    // RED PHASE: Expect RST to be low or 0.0 due to underflow
    assert(snap.summary.rst_encoder_s < 10.0f);

    tsa_destroy(h);
    printf("test_eb_underflow_detection passed.\n");
}

int main() {
    test_eb_underflow_detection();
    return 0;
}
