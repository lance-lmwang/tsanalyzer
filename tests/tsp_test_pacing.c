#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsp.h"

uint64_t tsp_get_udp_rate_scaled(tsp_handle_t* h);

void set_pcr(uint8_t* pkt, uint64_t pcr_val) {
    uint64_t pcr_base = pcr_val / 300;
    uint16_t pcr_ext = pcr_val % 300;
    pkt[6] = (pcr_base >> 25) & 0xFF;
    pkt[7] = (pcr_base >> 17) & 0xFF;
    pkt[8] = (pcr_base >> 9) & 0xFF;
    pkt[9] = (pcr_base >> 1) & 0xFF;
    pkt[10] = ((pcr_base & 0x01) << 7) | ((pcr_ext >> 8) & 0x01);
    pkt[11] = pcr_ext & 0xFF;
}

void test_dynamic_pacing() {
    setenv("TSPACER_SKIP_HARDENING", "1", 1);

    tsp_config_t cfg = {0};
    cfg.bitrate = 50000000;  // 50 Mbps Limit
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 1234;
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    uint64_t initial_rate = tsp_get_udp_rate_scaled(h);
    printf("Initial Rate Scaled: %lu\n", initial_rate);
    assert(initial_rate == 0);

    // Calculate expected PPS for 50 Mbps limit
    // 50,000,000 / (188 * 8 * 7) = 4749
    uint64_t expected_limit_pps = 50000000ULL / (188 * 8 * 7);

    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x20, 0x07, 0x10};
    uint8_t payload[188] = {0x47, 0x01, 0x00, 0x10};

    // 1. Detect 20 Mbps
    set_pcr(pkt, 0);
    tsp_enqueue(h, pkt, 1);
    for (int i = 0; i < 99; i++) tsp_enqueue(h, payload, 1);
    set_pcr(pkt, 203040);  // Delta PCR for 20Mbps at 100 packets
    tsp_enqueue(h, pkt, 1);

    uint64_t detected = tsp_get_detected_bitrate(h);
    uint64_t updated_rate = tsp_get_udp_rate_scaled(h);
    printf("Detected: %lu bps, Updated Rate Scaled: %lu\n", detected, updated_rate);
    assert(detected >= 19900000 && detected <= 20100000);
    assert(updated_rate > 0);

    // 2. Detect 100 Mbps (Should be limited to 50 Mbps)
    for (int i = 0; i < 99; i++) tsp_enqueue(h, payload, 1);
    set_pcr(pkt, 203040 + 40608);
    tsp_enqueue(h, pkt, 1);

    detected = tsp_get_detected_bitrate(h);
    uint64_t final_rate = tsp_get_udp_rate_scaled(h);
    printf("Detected: %lu bps, Final Rate Scaled: %lu, Expected Limit: %lu\n", detected, final_rate,
           expected_limit_pps);

    assert(detected >= 99000000 && detected <= 101000000);
    assert(final_rate == expected_limit_pps);

    tsp_destroy(h);
    printf("Dynamic pacing tests passed!\n");
}

int main() {
    test_dynamic_pacing();
    return 0;
}
