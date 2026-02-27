#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsp.h"

void test_rate_estimation() {
    // Skip mlockall and other RT hardening for the test
    setenv("TSPACER_SKIP_HARDENING", "1", 1);

    tsp_config_t cfg = {0};
    cfg.bitrate = 100000000;  // 100 Mbps Limit
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 1234;
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188] = {0};
    pkt[0] = 0x47;
    pkt[1] = 0x01;  // PID = 0x100
    pkt[2] = 0x00;
    pkt[3] = 0x20;  // AFC = 2
    pkt[4] = 7;
    pkt[5] = 0x10;  // PCR flag

    // First PCR: value = 0
    memset(pkt + 6, 0, 6);
    tsp_enqueue(h, pkt, 1);

    // Enqueue some payload packets (PID 0x100)
    uint8_t payload[188] = {0};
    payload[0] = 0x47;
    payload[1] = 0x01;
    payload[2] = 0x00;
    payload[3] = 0x10;  // No AF
    for (int i = 0; i < 99; i++) {
        tsp_enqueue(h, payload, 1);
    }

    // Second PCR at packet 100 (99 payload + 1 original)
    // Delta Bytes = 100 * 188 = 18800
    // Target Bitrate = 20 Mbps = 20,000,000 bps
    // Delta PCR = Delta Bytes * 8 * 27,000,000 / Bitrate
    // Delta PCR = 18800 * 8 * 27,000,000 / 20,000,000 = 203,040
    uint64_t delta_pcr = 203040;
    uint64_t pcr_base = delta_pcr / 300;
    uint16_t pcr_ext = delta_pcr % 300;

    pkt[6] = (pcr_base >> 25) & 0xFF;
    pkt[7] = (pcr_base >> 17) & 0xFF;
    pkt[8] = (pcr_base >> 9) & 0xFF;
    pkt[9] = (pcr_base >> 1) & 0xFF;
    pkt[10] = ((pcr_base & 0x01) << 7) | ((pcr_ext >> 8) & 0x01);
    pkt[11] = pcr_ext & 0xFF;

    tsp_enqueue(h, pkt, 1);

    uint64_t detected = tsp_get_detected_bitrate(h);
    printf("Detected Bitrate: %lu bps\n", detected);

    // Check if detected bitrate is around 20 Mbps
    assert(detected >= 19900000 && detected <= 20100000);

    tsp_destroy(h);
    printf("Rate estimation tests passed!\n");
}

int main() {
    test_rate_estimation();
    return 0;
}
