#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "tsa_internal.h"

/* Helper to simulate a stream being processed using PUBLIC API */
void feed_pcr_via_api(tsa_handle_t* h, uint64_t pcr_ticks, uint64_t now_ns, int pkts_to_add) {
    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = 0x01; // PID 0x100 (part 1)
    pkt[2] = 0x00; // PID 0x100 (part 2)
    pkt[3] = 0x20; // AF only
    pkt[4] = 7;    // AF length
    pkt[5] = 0x10; // PCR flag
    
    uint64_t base = pcr_ticks / 300;
    uint16_t ext = pcr_ticks % 300;
    pkt[6] = (base >> 25) & 0xFF;
    pkt[7] = (base >> 17) & 0xFF;
    pkt[8] = (base >> 9) & 0xFF;
    pkt[9] = (base >> 1) & 0xFF;
    pkt[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    pkt[11] = ext & 0xFF;

    // Simulate dummy payload packets before the PCR packet to build the count
    uint8_t plain_pkt[188];
    memset(plain_pkt, 0, 188);
    plain_pkt[0] = 0x47;
    plain_pkt[1] = 0x01; 
    plain_pkt[2] = 0x00;
    plain_pkt[3] = 0x10; // Payload only
    
    for (int i = 0; i < pkts_to_add - 1; i++) {
        tsa_feed_data(h, plain_pkt, 188, now_ns);
    }
    
    // Now feed the PCR packet
    tsa_feed_data(h, pkt, 188, now_ns);
}

int main() {
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h);

    printf(">>> Testing Bitrate Consistency Logic via Public API...\n");

    uint64_t pcr = 27000000ULL;
    uint64_t wall = 1000000000ULL;

    /* 1. Established a steady 10 Mbps baseline */
    /* 10 Mbps = 10,000,000 bits/s = 6648.9 pkts/s
     * Let's send 1000 packets per 150ms PCR window -> ~10.02 Mbps */
    for (int i = 0; i < 20; i++) {
        feed_pcr_via_api(h, pcr, wall, 1000); 
        pcr += (27000 * 150); // 150ms interval
        wall += 150000000ULL;
    }
    
    uint64_t initial_br = h->live->pcr_bitrate_bps;
    printf("[INFO] Initial Measured Bitrate: %lu bps\n", initial_br);
    assert(initial_br > 9000000);

    /* 2. Introduce Heavy Physical Jitter
     * We vary the 'wall' time (arrival), but keep PCR domain constant. */
    for (int i = 0; i < 10; i++) {
        uint64_t jitter_wall = wall + (i % 2 == 0 ? 50000000 : -20000000); // jitter +/- 50ms
        feed_pcr_via_api(h, pcr, jitter_wall, 1000);
        pcr += (27000 * 150);
        wall += 150000000ULL;
    }

    printf("[INFO] Bitrate after Jitter: %lu bps\n", h->live->pcr_bitrate_bps);
    // The bitrate MUST remain EXACTLY the same because it's locked to PCR domain
    assert(h->live->pcr_bitrate_bps == initial_br);
    printf("[PASS] Bitrate is deterministic and decoupled from physical arrival time.\n");

    tsa_destroy(h);
    printf("\n>>> ALL BITRATE CONSISTENCY TESTS PASSED <<<\n");
    return 0;
}
