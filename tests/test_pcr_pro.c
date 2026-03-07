#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void encode_pcr_full(uint8_t* pkt, uint64_t ticks) {
    uint64_t base = ticks / 300;
    uint16_t ext = ticks % 300;
    pkt[6] = (base >> 25) & 0xFF;
    pkt[7] = (base >> 17) & 0xFF;
    pkt[8] = (base >> 9) & 0xFF;
    pkt[9] = (base >> 1) & 0xFF;
    pkt[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    pkt[11] = ext & 0xFF;
}

void test_pcr_accuracy_threshold() {
    printf("Running test_pcr_accuracy_threshold...\n");
    tsa_config_t cfg = {0};
    cfg.analysis.pcr_ema_alpha = 0.1;
    tsa_handle_t* h = tsa_create(&cfg);
    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x30, 0x07, 0x10};  // PID 0x100, AF+PCR
    h->live->pid_is_referenced[0x100] = true;

    uint64_t start_ns = 1000000000ULL;
    // 1. Process 20 packets with NO jitter (linear)
    for (int i = 0; i < 20; i++) {
        uint64_t pcr_ticks = (uint64_t)i * 20 * 27000;  // 20ms intervals
        encode_pcr_full(pkt, pcr_ticks);
        tsa_process_packet(h, pkt, start_ns + (uint64_t)i * 20000000ULL);
    }

    tsa_commit_snapshot(h, start_ns + 400000000ULL);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    printf("Accuracy (Clean): %.2f ns, Errors: %lu\n", snap.stats.pcr_accuracy_ns, snap.stats.pcr_accuracy_error.count);
    assert(snap.stats.pcr_accuracy_ns < 100.0);
    assert(snap.stats.pcr_accuracy_error.count == 0);

    // 2. Inject ONE packet with 1000ns jitter
    uint64_t pcr_ticks_jitter = 20 * 20 * 27000 + (1000 * 27 / 1000);
    encode_pcr_full(pkt, pcr_ticks_jitter);
    tsa_process_packet(h, pkt, start_ns + 20 * 20000000ULL);

    tsa_commit_snapshot(h, start_ns + 420000000ULL);
    tsa_take_snapshot_full(h, &snap);
    printf("Accuracy (Jitter): %.2f ns, Errors: %lu\n", snap.stats.pcr_accuracy_ns,
           snap.stats.pcr_accuracy_error.count);

    // Accuracy should reflect the 1000ns jitter
    assert(snap.stats.pcr_accuracy_ns >= 500.0);
    assert(snap.stats.pcr_accuracy_error.count > 0);

    tsa_destroy(h);
}

int main() {
    test_pcr_accuracy_threshold();
    return 0;
}
