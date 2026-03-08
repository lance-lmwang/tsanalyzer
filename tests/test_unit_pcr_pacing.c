#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "tsa.h"
#include "tsa_internal.h"

/* Export internal functions for testing */
extern void tsa_process_packet(tsa_handle_t* h, const uint8_t* p, uint64_t n);

static void make_packet(uint8_t* buf, uint16_t pid, uint8_t cc) {
    memset(buf, 0, 188);
    buf[0] = 0x47;
    buf[1] = (pid >> 8) & 0x1F;
    buf[2] = pid & 0xFF;
    buf[3] = 0x10 | (cc & 0x0F);
}

static void make_pcr_packet(uint8_t* buf, uint16_t pid, uint64_t pcr_ticks, uint8_t cc) {
    memset(buf, 0, 188);
    buf[0] = 0x47;
    buf[1] = (pid >> 8) & 0x1F;
    buf[2] = pid & 0xFF;
    buf[3] = 0x30 | (cc & 0x0F);
    buf[4] = 7;
    buf[5] = 0x10;
    uint64_t base = pcr_ticks / 300;
    uint16_t ext = pcr_ticks % 300;
    buf[6] = (base >> 25) & 0xFF;
    buf[7] = (base >> 17) & 0xFF;
    buf[8] = (base >> 9) & 0xFF;
    buf[9] = (base >> 1) & 0xFF;
    buf[10] = ((base & 1) << 7) | ((ext >> 8) & 0x01);
    buf[11] = ext & 0xFF;
}

int main() {
    printf(">>> STARTING PCR PACING UNIT TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    uint8_t pkt[188];

    /*
     * Target Scenario: 10 Mbps Stream
     * 10 Mbps = 1,250,000 bytes/s = ~6648.9 packets/s
     * Ticks Per Second = 27,000,000
     * Theoretical Ticks Per Packet = 27,000,000 / 6648.9 = ~4060.8 ticks
     */

    printf("   Step 1: Simulating 10Mbps stream...\n");
    make_pcr_packet(pkt, 0x100, 0, 0);
    tsa_process_packet(h, pkt, 0);

    // Feed 1000 packets
    for (int i = 1; i <= 1000; i++) {
        make_packet(pkt, 0x101, i % 16);
        tsa_process_packet(h, pkt, 0);
    }

    /* 1000 packets at 10Mbps takes: (1000 * 188 * 8) / 10,000,000 = 0.1504 seconds
     * 0.1504s * 27,000,000 = 4,060,800 ticks */
    uint64_t next_pcr = 4060800;
    make_pcr_packet(pkt, 0x100, next_pcr, 1);
    tsa_process_packet(h, pkt, 0);

    uint64_t tpp_q16 = h->clock_inspectors[0x100].br_est.ticks_per_packet_q16;
    double tpp = (double)tpp_q16 / 65536.0;

    printf("   Calculated Ticks Per Packet: %.4f (Expected: ~4060.8)\n", tpp);

    /* 🚨 VERIFICATION: Should be within 0.1% of theoretical value */
    if (tpp < 4050.0 || tpp > 4070.0) {
        printf("[FAIL] TicksPerPacket calculation deviation too high!\n");
        return 1;
    }

    printf(">>> PCR PACING TEST PASSED <<<\n");
    tsa_destroy(h);
    return 0;
}
