#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_high_freq_pcr_stc_lock() {
    printf("Running High-Frequency PCR Stress Test...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188] = {0};
    pkt[0] = 0x47;
    pkt[1] = 0x01;  // PID 0x100
    pkt[3] = 0x30;  // AF + Payload
    pkt[4] = 7;     // AF length
    pkt[5] = 0x10;  // PCR flag

    uint64_t base_pcr = TS_SYSTEM_CLOCK_HZ * 10;  // 10s start
    uint64_t sys_time = NS_PER_SEC;               // 1s start

    // Feed 100 high-frequency PCRs (every 10 packets)
    for (int i = 0; i < 200; i++) {
        uint64_t current_pcr = base_pcr + (i * 10000);  // Fast increment
        pkt[6] = (current_pcr >> 25) & 0xFF;
        pkt[7] = (current_pcr >> 17) & 0xFF;
        pkt[8] = (current_pcr >> 9) & 0xFF;
        pkt[9] = (current_pcr >> 1) & 0xFF;
        pkt[10] = ((current_pcr << 7) & 0x80) | 0x7E;
        pkt[11] = 0;

        tsa_process_packet(h, pkt, sys_time + (i * 100000));

        if (i > 20) {
            assert(h->stc_locked == true);
        }
    }

    printf("STC Lock: %s, Slope: %.6f\n", h->stc_locked ? "YES" : "NO", FROM_Q64_64(h->stc_slope_q64));
    assert(h->stc_locked == true);
    // Ensure slope is reasonable (around 1.0)
    double slope = FROM_Q64_64(h->stc_slope_q64);
    (void)slope;
    assert(slope > 0.9 && slope < 1.1);

    tsa_destroy(h);
}

void test_tstd_overflow_detection() {
    printf("Running T-STD Overflow Verification...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    // Mock a Video PID 0x100
    h->pid_stream_type[0x100] = 0x1b;  // H.264
    h->live->pid_is_referenced[0x100] = true;
    tsa_update_pid_tracker(h, 0x100);

    // Manually stuff the EB (Elementary Buffer)
    // In a real scenario, this happens via tsa_handle_es_payload
    // Here we test the draining logic dependency on STC
    h->es_tracks[0x100].tstd.eb_fill_q64 = INT_TO_Q64_64(1000000);  // 1MB full

    // Set STC but don't advance it yet
    h->stc_ns = NS_PER_SEC;
    h->last_snap_ns = 900000000ULL;

    // Snapshot should drain nothing because STC hasn't reached any DTS
    tsa_commit_snapshot(h, NS_PER_SEC);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    int idx = tsa_find_pid_in_snapshot(&snap, 0x100);
    assert(idx != -1);
    printf("EB Fill after no-advance: %.2f%%\n", snap.pids[idx].eb_fill_pct);
    assert(snap.pids[idx].eb_fill_pct > 80.0);  // Should remain high

    // Now advance STC past some DTS
    h->es_tracks[0x100].au_q.queue[0].dts_ns = 1100000000ULL;
    h->es_tracks[0x100].au_q.queue[0].size = 500000;
    h->es_tracks[0x100].au_q.head = 0;
    h->es_tracks[0x100].au_q.tail = 1;

    h->stc_ns = 1200000000ULL;  // Past DTS
    tsa_commit_snapshot(h, 1200000000ULL);
    tsa_take_snapshot_full(h, &snap);

    printf("EB Fill after STC advance: %.2f%%\n", snap.pids[idx].eb_fill_pct);
    // 1MB - 0.5MB = 0.5MB. 0.5MB / 1.2MB total ~ 41%
    assert(snap.pids[idx].eb_fill_pct < 50.0);

    tsa_destroy(h);
}

int main() {
    test_high_freq_pcr_stc_lock();
    test_tstd_overflow_detection();
    printf("Stress and T-STD tests passed!\n");
    return 0;
}
