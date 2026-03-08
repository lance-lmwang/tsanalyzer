#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tsa.h"

#define TS_PKT_SIZE 188
#define PID_A 0x100
#define PID_B 0x200

void generate_pcr_pkt(uint8_t* buf, uint16_t pid, uint64_t pcr_ticks) {
    memset(buf, 0xFF, TS_PKT_SIZE);
    buf[0] = 0x47;
    buf[1] = (pid >> 8) & 0x1F;
    buf[2] = pid & 0xFF;
    buf[3] = 0x30;
    buf[4] = 0x07;
    buf[5] = 0x10;
    uint64_t base = pcr_ticks / 300;
    uint16_t ext = pcr_ticks % 300;
    buf[6] = (base >> 25) & 0xFF;
    buf[7] = (base >> 17) & 0xFF;
    buf[8] = (base >> 9) & 0xFF;
    buf[9] = (base >> 1) & 0xFF;
    buf[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    buf[11] = ext & 0xFF;
}

void test_mpts_bitrate_isolation() {
    tsa_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.op_mode = TSA_MODE_REPLAY;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[TS_PKT_SIZE];
    uint64_t now_ns = 1000000000ULL;

    /*
     * Simulation: 10Mbps total.
     * 500ms duration.
     * PID A (40%): 4Mbps
     * PID B (60%): 6Mbps
     */
    uint64_t duration_ms = 500;
    uint64_t interval_ticks = (duration_ms * 27000);
    uint64_t total_pkts = (10000000ULL * duration_ms / 1000) / (188 * 8);
    uint64_t pkts_a = (total_pkts * 4) / 10;
    uint64_t pkts_b = total_pkts - pkts_a;

    printf(">>> Testing MPTS Isolation: Total=%lu, PID_A=%lu, PID_B=%lu\n", total_pkts, pkts_a, pkts_b);

    // Initial Sync
    generate_pcr_pkt(pkt, PID_A, 0); tsa_process_packet(h, pkt, now_ns);
    generate_pcr_pkt(pkt, PID_B, 0); tsa_process_packet(h, pkt, now_ns);

    // Data Phase
    for (uint64_t i = 0; i < total_pkts; i++) {
        now_ns += 150000;
        memset(pkt, 0xFF, TS_PKT_SIZE);
        uint16_t pid = (i < pkts_a) ? PID_A : PID_B;
        pkt[0] = 0x47; pkt[1] = (pid >> 8) & 0x1F; pkt[2] = pid & 0xFF; pkt[3] = 0x10;
        tsa_process_packet(h, pkt, now_ns);
    }

    // Final PCR Settlement
    generate_pcr_pkt(pkt, PID_A, interval_ticks); tsa_process_packet(h, pkt, now_ns);
    generate_pcr_pkt(pkt, PID_B, interval_ticks); tsa_process_packet(h, pkt, now_ns);

    // Force commit to export live stats
    tsa_commit_snapshot(h, now_ns);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    double br_a = (double)snap.stats.pid_bitrate_bps[PID_A] / 1e6;
    double br_b = (double)snap.stats.pid_bitrate_bps[PID_B] / 1e6;
    double br_total = (double)snap.summary.pcr_bitrate_bps / 1e6;

    printf("PID A Bitrate: %.2f Mbps (Expected ~4.0)\n", br_a);
    printf("PID B Bitrate: %.2f Mbps (Expected ~6.0)\n", br_b);
    printf("Total PCR Bitrate: %.2f Mbps (Expected ~10.0)\n", br_total);

    /*
     * Critical assertions:
     * If bug existed, PID A would use global total_pkts (3324) instead of pkts_a (1329),
     * calculating ~10Mbps for PID A alone.
     */
    assert(br_a > 3.0 && br_a < 5.0);
    assert(br_b > 5.0 && br_b < 7.0);
    assert(br_total > 9.0 && br_total < 11.0);

    printf(">>> MPTS Isolation Test PASSED!\n");
    tsa_destroy(h);
}

int main() {
    test_mpts_bitrate_isolation();
    return 0;
}
