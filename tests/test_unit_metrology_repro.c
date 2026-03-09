#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/* Helper to create a basic TS packet */
static void make_packet(uint8_t* buf, uint16_t pid) {
    memset(buf, 0, 188);
    buf[0] = 0x47;
    buf[1] = (pid >> 8) & 0x1F;
    buf[2] = pid & 0xFF;
    buf[3] = 0x10;  // Payload only
}

static void make_pcr_packet(uint8_t* buf, uint16_t pid, uint64_t pcr_val) {
    make_packet(buf, pid);
    buf[3] = 0x30;  // Adapt + Payload
    buf[4] = 7;
    buf[5] = 0x10;  // PCR flag
    uint64_t base = pcr_val / 300;
    uint16_t ext = pcr_val % 300;
    buf[6] = (base >> 25) & 0xFF;
    buf[7] = (base >> 17) & 0xFF;
    buf[8] = (base >> 9) & 0xFF;
    buf[9] = (base >> 1) & 0xFF;
    buf[10] = ((base & 1) << 7) | 0x7E | ((ext >> 8) & 1);
    buf[11] = ext & 0xFF;
}

static int test_repro_physical_metrics() {
    printf("Checking Physical Bitrate & Filtering...\n");
    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_REPLAY;
    tsa_handle_t* h = tsa_create(&cfg);
    uint8_t pkt[188];
    uint64_t now = 1000000000ULL;

    /* Feed 13298 packets at 20Mbps (approx 1 second) */
    for (int i = 0; i < 13298; i++) {
        now += 75200;
        make_packet(pkt, 0x100);
        tsa_process_packet(h, pkt, now);
    }

    tsa_commit_snapshot(h, now);
    printf("   Total Packets: %lu (Expected 13298)\n", (unsigned long)h->live->total_ts_packets);
    printf("   Physical Rate: %lu bps (Expected ~20Mbps)\n", (unsigned long)h->live->physical_bitrate_bps);

    if (h->live->total_ts_packets != 13298) return 1;
    if (h->live->physical_bitrate_bps < 19000000 || h->live->physical_bitrate_bps > 21000000) return 1;

    tsa_destroy(h);
    return 0;
}

static int test_repro_mpts_collision() {
    printf("Checking MPTS PCR Collision...\n");
    tsa_config_t cfg1 = {0}, cfg2 = {0};
    cfg1.op_mode = TSA_MODE_REPLAY;
    cfg2.op_mode = TSA_MODE_REPLAY;

    /* Handle 1: Standard SPTS */
    tsa_handle_t* h1 = tsa_create(&cfg1);
    uint8_t pkt[188];
    uint64_t now1 = 1000000000ULL;

    for (int i = 0; i < 13298; i++) {
        now1 += 75200;
        if (i % 500 == 0)
            make_pcr_packet(pkt, 0x100, (uint64_t)i * 1504ULL * 27000000ULL / 20000000ULL);
        else
            make_packet(pkt, 0x100);
        tsa_process_packet(h1, pkt, now1);
    }
    h1->stc_locked = true; /* Force lock for test */
    tsa_commit_snapshot(h1, now1);
    printf("   SPTS Rate: %lu bps\n", (unsigned long)h1->live->pcr_bitrate_bps);

    /* Handle 2: MPTS Collision Simulation */
    tsa_handle_t* h2 = tsa_create(&cfg2);
    uint8_t pkt_pcr[188], dummy[188];
    make_packet(dummy, 0x50);
    uint64_t now2 = 1000000000ULL;

    for (int i = 0; i < 13298; i++) {
        now2 += 75200;
        if (i % 500 == 0) {
            make_pcr_packet(pkt_pcr, 0x100, (uint64_t)i * 1504ULL * 27000000ULL / 20000000ULL);
            tsa_process_packet(h2, pkt_pcr, now2);
        } else if (i % 700 == 0) {
            make_pcr_packet(pkt_pcr, 0x200, (uint64_t)i * 1504ULL * 27000000ULL / 20000000ULL);
            tsa_process_packet(h2, pkt_pcr, now2);
        } else {
            make_packet(dummy, 0x50);
            tsa_process_packet(h2, dummy, now2);
        }
    }
    h2->stc_locked = true; /* Force lock for test */
    tsa_commit_snapshot(h2, now2);
    printf("   MPTS Rate: %lu bps\n", (unsigned long)h2->live->pcr_bitrate_bps);

    int failed = 0;
    if (h1->live->pcr_bitrate_bps < 19000000) failed = 1;
    if (h2->live->pcr_bitrate_bps < 19000000) failed = 1;

    tsa_destroy(h1);
    tsa_destroy(h2);
    return failed;
}

int main() {
    printf(">>> STARTING METROLOGY REPRODUCTION UNIT TESTS <<<\n");
    int failed = 0;
    failed |= test_repro_physical_metrics();
    failed |= test_repro_mpts_collision();
    if (failed) {
        printf(">>> END OF TESTS: BUGS REPRODUCED <<<\n");
        return 1;
    }
    printf(">>> END OF TESTS: ALL PASSED <<<\n");
    return 0;
}
