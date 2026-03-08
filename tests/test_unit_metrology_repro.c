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
    buf[3] = 0x10; /* CC=0, no AF */
}

/* Helper to create a PCR packet */
static void make_pcr_packet(uint8_t* buf, uint16_t pid, uint64_t pcr_27mhz) {
    make_packet(buf, pid);
    buf[3] = 0x30; /* Adaptation field only */
    buf[4] = 7;    /* AF length */
    buf[5] = 0x10; /* PCR flag */
    uint64_t base = pcr_27mhz / 300;
    uint16_t ext = pcr_27mhz % 300;
    buf[6] = (base >> 25) & 0xFF;
    buf[7] = (base >> 17) & 0xFF;
    buf[8] = (base >> 9) & 0xFF;
    buf[9] = (base >> 1) & 0xFF;
    buf[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    buf[11] = ext & 0xFF;
}

/* Reproduction A & B: Physical Bitrate and Filtering issues */
void test_repro_physical_metrics() {
    printf("Checking Physical Bitrate & Filtering...\n");
    tsa_config_t cfg = {0};
    /* Enable filtering to trigger Bug B */
    cfg.analysis.enable_reactive_pid_filter = true;
    tsa_handle_t* h = tsa_create(&cfg);

    uint8_t pkt[188];
    uint64_t now = 1000000000ULL; /* 1s */

    /* Feed 13298 packets (approx 20Mbps for 1 second) */
    /* Use PID 0x1FFF (Stuffing) which is usually filtered out */
    for (int i = 0; i < 13298; i++) {
        make_packet(pkt, 0x1FFF);
        tsa_process_packet(h, pkt, now);
        now += 75200; /* ~75us per packet for 20Mbps */
    }

    /* Trigger snapshot at T=2s */
    tsa_commit_snapshot(h, 2000000000ULL);

    tsa_snapshot_lite_t snap;
    tsa_take_snapshot_lite(h, &snap);

    printf("   Total Packets: %lu\n", (unsigned long)snap.total_packets);
    printf("   Physical Rate: %lu bps\n", (unsigned long)snap.physical_bitrate_bps);

    /* BUG B Check: If filtered packets aren't counted, total_packets will be 0 */
    if (snap.total_packets == 0) {
        printf("[FAIL] Bug B Reproduced: Filtered packets (0x1FFF) not counted!\n");
    }

    /* BUG A Check: If timestamps or logic is wrong, rate will be 0 */
    if (snap.physical_bitrate_bps == 0) {
        printf("[FAIL] Bug A Reproduced: Physical bitrate reported as 0!\n");
    }

    tsa_destroy(h);
}

/* Reproduction C: MPTS PCR Collision */
void test_repro_mpts_collision() {
    printf("Checking MPTS PCR Collision...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    uint8_t pkt[188], dummy[188];
    make_packet(dummy, 0x50);

    /* Target: 20Mbps. 100ms interval = 1330 packets */

    /* 1. PID 0x100 First PCR (T=0) */
    make_pcr_packet(pkt, 0x100, 0);
    tsa_process_packet(h, pkt, 0);

    /* 2. PID 0x200 Interfering PCR (T=10ms) after 133 packets */
    for (int i = 0; i < 133; i++) tsa_process_packet(h, dummy, 0);
    make_pcr_packet(pkt, 0x200, 270000);
    tsa_process_packet(h, pkt, 0);

    /* 3. PID 0x100 Second PCR (T=100ms) after 1196 more packets */
    for (int i = 0; i < 1196; i++) tsa_process_packet(h, dummy, 0);
    make_pcr_packet(pkt, 0x100, 2700000);
    tsa_process_packet(h, pkt, 0);

    printf("   PCR Bitrate: %lu bps\n", (unsigned long)h->live->pcr_bitrate_bps);

    /* BUG C Check: Global state collision usually results in 0 or wildly wrong rate */
    if (h->live->pcr_bitrate_bps < 19000000 || h->live->pcr_bitrate_bps > 21000000) {
        printf("[FAIL] Bug C Reproduced: MPTS PCR bitrate is incorrect (%lu)!\n",
               (unsigned long)h->live->pcr_bitrate_bps);
    } else {
        printf("[PASS] MPTS Bitrate isolation works.\n");
    }

    tsa_destroy(h);
}

int main() {
    printf(">>> STARTING METROLOGY REPRODUCTION UNIT TESTS <<<\n");
    int failed = 0;

    /* Reproduction A & B */
    printf("Checking Physical Bitrate & Filtering...\n");
    tsa_config_t cfg1 = {0};
    cfg1.analysis.enable_reactive_pid_filter = true;
    tsa_handle_t* h1 = tsa_create(&cfg1);
    uint8_t pkt[188];
    uint64_t now = 1000000000ULL;
    for (int i = 0; i < 13298; i++) {
        make_packet(pkt, 0x1FFF);
        tsa_process_packet(h1, pkt, now);
        now += 75200;
    }
    tsa_commit_snapshot(h1, 2000000000ULL);
    tsa_snapshot_lite_t snap;
    tsa_take_snapshot_lite(h1, &snap);
    if (snap.total_packets == 0 || snap.physical_bitrate_bps == 0) {
        printf("[FAIL] Physical Bitrate or Filtering Bug Detected!\n");
        failed = 1;
    }

    /* Reproduction C */
    printf("Checking MPTS PCR Collision...\n");
    tsa_config_t cfg2 = {0};
    tsa_handle_t* h2 = tsa_create(&cfg2);
    uint8_t pkt_pcr[188], dummy[188];
    make_packet(dummy, 0x50);
    make_pcr_packet(pkt_pcr, 0x100, 0);
    tsa_process_packet(h2, pkt_pcr, 0);
    for (int i = 0; i < 133; i++) tsa_process_packet(h2, dummy, 0);
    make_pcr_packet(pkt_pcr, 0x200, 270000);
    tsa_process_packet(h2, pkt_pcr, 0);
    for (int i = 0; i < 1196; i++) tsa_process_packet(h2, dummy, 0);
    make_pcr_packet(pkt_pcr, 0x100, 2700000);
    tsa_process_packet(h2, pkt_pcr, 0);
    if (h2->live->pcr_bitrate_bps < 19000000 || h2->live->pcr_bitrate_bps > 21000000) {
        printf("[FAIL] MPTS PCR Collision Bug Detected! Rate: %lu\n", (unsigned long)h2->live->pcr_bitrate_bps);
        failed = 1;
    }

    tsa_destroy(h1);
    tsa_destroy(h2);

    if (failed) {
        printf(">>> END OF TESTS: BUGS REPRODUCED <<<\n");
        return 1;
    }

    printf(">>> END OF TESTS: ALL PASSED <<<\n");
    return 0;
}
