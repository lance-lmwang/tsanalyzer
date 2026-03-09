/**
 * @file test_pcr_reselection.c
 * @brief Regression test for Master PCR Reselection and Bitrate Aging.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/**
 * @brief Utility to feed a packet with PCR into the analyzer.
 */
static void feed_pcr_pkt(tsa_handle_t *h, uint16_t pid, uint64_t pcr_val_27mhz, uint64_t now_ns) {
    uint8_t pkt[188] = {0};
    pkt[0] = 0x47;
    pkt[1] = (pid >> 8) & 0x1F;
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x30;  // Adapt + Payload
    pkt[4] = 7;     // AF length
    pkt[5] = 0x10;  // PCR flag

    uint64_t base = pcr_val_27mhz / 300;
    uint16_t ext = pcr_val_27mhz % 300;

    pkt[6] = (uint8_t)(base >> 25);
    pkt[7] = (uint8_t)(base >> 17);
    pkt[8] = (uint8_t)(base >> 9);
    pkt[9] = (uint8_t)(base >> 1);
    pkt[10] = (uint8_t)(((base & 1) << 7) | 0x7E | ((ext >> 8) & 1));
    pkt[11] = (uint8_t)(ext & 0xFF);

    tsa_process_packet(h, pkt, now_ns);
}

/**
 * @brief Verifies that if the Master PCR PID disappears for > 5s,
 * the lock is released and a new Master can be selected.
 */
static void test_master_reselection_on_timeout() {
    printf("Testing Master PCR reselection on timeout (5s)...\n");

    tsa_config_t cfg = {0};
    strcpy(cfg.input_label, "reselection-test");
    cfg.analysis.enabled = true;
    cfg.op_mode = TSA_MODE_LIVE;

    tsa_handle_t *h = tsa_create(&cfg);
    uint64_t now = 1000000000ULL;  // Start at 1s

    // 1. Initial lock to PID 100
    feed_pcr_pkt(h, 100, 100 * TS_SYSTEM_CLOCK_HZ, now);
    assert(h->master_pcr_pid == 100);
    assert(h->stc_locked == true);
    printf("  [PASS] Initial lock to PID 100 established.\n");

    // 2. Continuous flow of PID 100 and a candidate PID 200
    now += 100000000ULL;  // +100ms
    feed_pcr_pkt(h, 100, 100 * TS_SYSTEM_CLOCK_HZ + 2700000ULL, now);
    feed_pcr_pkt(h, 200, 200 * TS_SYSTEM_CLOCK_HZ, now);
    assert(h->master_pcr_pid == 100);  // Lock should remain on 100

    // 3. Simulated blackout of PID 100 for 6 seconds
    // Only PID 200 continues to arrive.
    for (int i = 0; i < 60; i++) {
        now += 100000000ULL;  // 100ms steps
        feed_pcr_pkt(h, 200, 200 * TS_SYSTEM_CLOCK_HZ + (i + 1) * 2700000ULL, now);
    }

    // 4. Verify reselection
    // After 6s of absence, PID 100 should be released and PID 200 should take over.
    assert(h->master_pcr_pid == 200);
    assert(h->stc_locked == true);  // Should have re-locked to PID 200
    printf("  [PASS] Master successfully migrated to PID 200 after PID 100 timeout.\n");

    tsa_destroy(h);
}

/**
 * @brief Verifies that business bitrate aggregation uses a 2s window
 * to avoid overlap during rapid PID switching.
 */
static void test_bitrate_aging_2s() {
    printf("Testing Bitrate Aggregation Aging (2s window)...\n");

    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_LIVE;
    tsa_handle_t *h = tsa_create(&cfg);
    uint64_t now = 1000000000ULL;

    // 1. Establish bitrate for PID 300 (Active)
    for (int i = 0; i < 50; i++) {
        now += 10000000ULL;                           // 10ms
        uint8_t pkt[188] = {0x47, 0x01, 0x2C, 0x10};  // PID 300
        tsa_process_packet(h, pkt, now);
    }

    tsa_commit_snapshot(h, now);
    uint64_t initial_br = h->live->pid_bitrate_bps[300];
    assert(initial_br > 0);
    printf("  [INFO] PID 300 established at %lu bps.\n", initial_br);

    // 2. Stop PID 300 and wait for 2.1 seconds.
    now += 2100000000ULL;

    // Send a dummy packet from another PID to trigger snapshot logic
    uint8_t dummy[188] = {0x47, 0x1F, 0xFE, 0x10};
    tsa_process_packet(h, dummy, now);

    tsa_commit_snapshot(h, now);

    // Verify that PID 300 is no longer part of the aggregate pcr_bitrate_bps
    assert(h->live->pcr_bitrate_bps == 0);
    printf("  [PASS] PID 300 bitrate aged out after 2s (Aggregate is 0).\n");

    tsa_destroy(h);
}

int main() {
    printf("=== TSA REGRESSION: Master Reselection & Aging ===\n");
    test_master_reselection_on_timeout();
    test_bitrate_aging_2s();
    printf("SUMMARY: All reselection and aging tests PASSED.\n");
    return 0;
}
