#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_rst_tstd_integration() {
    printf("Running test_rst_tstd_integration...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188] = {0};
    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = 0x01;  // PID 0x100
    pkt[2] = 0x00;
    pkt[3] = TS_PAYLOAD_FLAG;

    // Simulate 500ms of data at 100Mbps
    // 100 Mbps = 12.5 MB/s = 12.5 bytes/us = 66 packets/ms
    // 500ms = 33,000 packets.
    uint64_t start_ns = 1000000000ULL;
    for (int i = 0; i < 33000; i++) {
        tsa_process_packet(h, pkt, start_ns + i * 15000ULL);  // ~15us gap = ~100Mbps
    }

    // Trigger snapshot commit
    tsa_commit_snapshot(h, start_ns + 500000000ULL);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    int idx = tsa_find_pid_in_snapshot(&snap, 0x100);
    assert(idx != -1);

    float rst_enc = snap.summary.rst_encoder_s;
    printf("RST Encoder: %.2f s (Fill: %.2f%%)\n", rst_enc, snap.pids[idx].tb_fill_pct);

    // TB should be filling because Input (100Mbps) > Leak (60Mbps = 1.2*50)
    // Expect RST < 900s
    assert(rst_enc < 10.0f);  // Should be very low given small TB (512B)

    tsa_destroy(h);
    printf("test_rst_tstd_integration passed.\n");
}

int main() {
    test_rst_tstd_integration();
    return 0;
}
