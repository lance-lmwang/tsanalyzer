#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa_internal.h"

static void set_pcr(uint8_t* pkt, uint64_t ticks) {
    uint64_t base = (ticks / 300) & 0x1FFFFFFFFULL;
    uint16_t ext = ticks % 300;
    pkt[6] = (base >> 25) & 0xFF;
    pkt[7] = (base >> 17) & 0xFF;
    pkt[8] = (base >> 9) & 0xFF;
    pkt[9] = (base >> 1) & 0xFF;
    pkt[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    pkt[11] = ext & 0xFF;
}

int main() {
    printf("Testing Piecewise PCR Accuracy...\n");
    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_REPLAY;
    cfg.analysis.pcr_ema_alpha = 0.1;
    tsa_handle_t* h = tsa_create(&cfg);

    // Create a dummy PCR packet
    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x30, 0x07, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint16_t pid = 0x100;
    pkt[1] = (pid >> 8) & 0x1F;
    pkt[2] = pid & 0xFF;

    // 1. Initial PCR
    set_pcr(pkt, 0);
    tsa_process_packet(h, pkt, 0);
    assert(h->last_pcr_ticks == 0);

    // 2. Feed 1000 payload packets (simulating constant bitrate)
    uint8_t pay[188] = {0x47, 0x01, 0x00, 0x10};
    pay[1] = (pid >> 8) & 0x1F;
    pay[2] = pid & 0xFF;
    for (int i = 0; i < 999; i++) {
        tsa_process_packet(h, pay, 0);
    }

    // 3. Second PCR (at 1000 packets)
    // 1000 pkts * 188 * 8 = 1,504,000 bits.
    // Assume 10 Mbps = 10,000,000 bps.
    // Duration = 0.1504 seconds.
    // PCR ticks = 0.1504 * 27,000,000 = 4,060,800 ticks.
    set_pcr(pkt, 4060800);
    tsa_process_packet(h, pkt, 0);

    printf("First interval bitrate: %lu bps\n", (unsigned long)h->last_pcr_interval_bitrate_bps);
    assert(h->last_pcr_interval_bitrate_bps == 10000000);

    // 4. Third PCR with JITTER
    // Feed another 1000 packets.
    for (int i = 0; i < 999; i++) {
        tsa_process_packet(h, pay, 0);
    }
    // Expected PCR = 4060800 + 4060800 = 8121600.
    // Actual PCR = 8121600 + 2700 ticks (100us jitter).
    set_pcr(pkt, 8121600 + 2700);
    tsa_process_packet(h, pkt, 0);

    printf("Piecewise Accuracy (ns): %.2f\n", h->live->pcr_accuracy_ns_piecewise);
    // 2700 ticks = 100,000 ns.
    assert(fabs(h->live->pcr_accuracy_ns_piecewise - 100000.0) < 10.0);

    tsa_destroy(h);
    printf("Piecewise PCR test passed!\n");
    return 0;
}
