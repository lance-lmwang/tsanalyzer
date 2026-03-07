#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_clock.h"
#include "tsa_internal.h"

void create_pcr_packet(uint8_t *pkt, uint16_t pid, uint64_t pcr_27mhz, uint8_t cc) {
    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47;
    pkt[1] = (pid >> 8) & 0x1F;
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x30 | (cc & 0x0F);  // Adaptation + Payload
    pkt[4] = 7;                   // AF length
    pkt[5] = 0x10;                // PCR flag

    uint64_t base = pcr_27mhz / 300;
    uint16_t ext = pcr_27mhz % 300;

    pkt[6] = (base >> 25) & 0xFF;
    pkt[7] = (base >> 17) & 0xFF;
    pkt[8] = (base >> 9) & 0xFF;
    pkt[9] = (base >> 1) & 0xFF;
    pkt[10] = ((base & 0x01) << 7) | ((ext >> 8) & 0x01);
    pkt[11] = ext & 0xFF;
}

int main() {
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t *h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t pcr_pid = 0x100;
    h->programs[0].pcr_pid = pcr_pid;

    uint8_t pkt[188];
    uint64_t pcr = 0;
    uint64_t now = 1000000000ULL;  // 1s

    printf("Step 1: Initializing PCR clock...\n");
    create_pcr_packet(pkt, pcr_pid, pcr, 0);
    tsa_process_packet(h, pkt, now);
    assert(h->clock_inspectors[pcr_pid].initialized == true);

    printf("Step 2: Sending normal PCRs (20ms interval)...\n");
    for (int i = 1; i <= 5; i++) {
        pcr += 20 * 27000;  // 20ms
        now += 20 * 1000000;
        create_pcr_packet(pkt, pcr_pid, pcr, i % 16);
        tsa_process_packet(h, pkt, now);
    }

    printf("PCR Interval Max: %lu ticks (expected ~540000)\n", h->clock_inspectors[pcr_pid].pcr_interval_max_ticks);
    assert(h->clock_inspectors[pcr_pid].priority_1_errors == 0);

    printf("Step 3: Simulating a 50ms gap to trigger 1.1 timeout...\n");
    // We don't call process_packet yet, we simulate the monitoring loop check
    now += 50 * 1000000;  // 50ms later
    h->stc_ns = now;      // Mock STC progress
    h->stc_locked = true;

    // Manually trigger a snapshot or the timeout logic we added
    // In tsa.c, the timeout logic is in the snapshot commit path or similar
    // Let's call tsa_commit_snapshot which contains the monitoring loop
    tsa_commit_snapshot(h, now);

    printf("PCR Repetition Errors: %lu\n", h->live->pcr_repetition_error.count);
    assert(h->live->alarm_pcr_repetition_error == true);
    assert(h->live->pcr_repetition_error.count > 0);

    printf("Step 4: Recovering with a valid PCR...\n");
    pcr += 50 * 27000;
    create_pcr_packet(pkt, pcr_pid, pcr, 6);
    tsa_process_packet(h, pkt, now);
    tsa_commit_snapshot(h, now);

    assert(h->live->alarm_pcr_repetition_error == false);

    printf("Step 5: Testing Discontinuity Indicator (should NOT trigger error)...\n");
    now += 100 * 1000000;  // 100ms gap (normally triggers error)
    pcr += 100 * 27000;
    create_pcr_packet(pkt, pcr_pid, pcr, 7);
    pkt[5] |= 0x80;  // Set discontinuity indicator

    uint32_t errors_before = h->clock_inspectors[pcr_pid].priority_1_errors;
    tsa_process_packet(h, pkt, now);
    tsa_commit_snapshot(h, now);

    assert(h->clock_inspectors[pcr_pid].priority_1_errors == errors_before);
    assert(h->live->alarm_pcr_repetition_error == false);

    printf("ClockInspector verification PASSED!\n");
    tsa_destroy(h);
    return 0;
}
