#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    printf(">>> STARTING PCR INTEGRITY UNIT TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    uint8_t pkt[188];

    /* Phase 1: Establish a stable baseline (10 Mbps) */
    printf("   Step 1: Establishing 10Mbps baseline...\n");
    make_pcr_packet(pkt, 0x100, 0, 0);
    tsa_process_packet(h, pkt, 0);
    for (int i = 1; i <= 665; i++) {
        make_packet(pkt, 0x101, i % 16);
        tsa_process_packet(h, pkt, 0);
    }
    make_pcr_packet(pkt, 0x100, 2700000, 1); /* 100ms later */
    tsa_process_packet(h, pkt, 0);

    uint64_t baseline = h->live->pcr_bitrate_bps;
    printf("   Baseline Rate: %lu bps\n", (unsigned long)baseline);
    assert(baseline > 9900000 && baseline < 10100000);

    /* Phase 2: Inject a CC error in the next window */
    printf("   Step 2: Injecting CC error into next window...\n");
    for (int i = 0; i < 300; i++) {
        make_packet(pkt, 0x101, i % 16);
        tsa_process_packet(h, pkt, 0);
    }

    // FORCED CC ERROR: Jump from CC=12 to CC=15
    make_packet(pkt, 0x101, 15);
    tsa_process_packet(h, pkt, 0);

    for (int i = 0; i < 364; i++) {
        make_packet(pkt, 0x101, i % 16);
        tsa_process_packet(h, pkt, 0);
    }

    // End PCR for this corrupted window
    make_pcr_packet(pkt, 0x100, 5400000, 2);
    tsa_process_packet(h, pkt, 0);

    uint64_t corrupted_rate = h->live->pcr_bitrate_bps;
    printf("   Rate after CC error: %lu bps\n", (unsigned long)corrupted_rate);

    /* 🚨 VERIFICATION: The rate should NOT have changed (window discarded)
     * or it should be 0 if it was the first window.
     * Here it should stay at the baseline value because we reuse last_bitrate_bps on discard. */
    if (corrupted_rate != baseline) {
        printf("[FAIL] Metrology window was NOT discarded after CC error!\n");
        return 1;
    }

    printf(">>> PCR INTEGRITY TEST PASSED <<<\n");
    tsa_destroy(h);
    return 0;
}
