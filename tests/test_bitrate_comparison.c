#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"

void encode_pcr(uint8_t* pkt, uint64_t pcr_ticks) {
    uint64_t base = pcr_ticks / 300;
    uint16_t ext = (uint16_t)(pcr_ticks % 300);
    pkt[3] = 0x30;  // AF + Payload
    pkt[4] = 7;     // AF length
    pkt[5] = 0x10;  // PCR flag
    pkt[6] = (uint8_t)((base >> 25) & 0xFF);
    pkt[7] = (uint8_t)((base >> 17) & 0xFF);
    pkt[8] = (uint8_t)((base >> 9) & 0xFF);
    pkt[9] = (uint8_t)((base >> 1) & 0xFF);
    pkt[10] = (uint8_t)(((base & 1) << 7) | 0x7E | ((ext >> 8) & 1));
    pkt[11] = (uint8_t)(ext & 0xFF);
}

int main() {
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);

    uint64_t target_bitrate = 10000000;  // 10Mbps
    uint64_t pkts_to_send = 20000;
    uint64_t interval_ns = (188ULL * 8 * 1000000000ULL) / target_bitrate;
    uint64_t now_ns = 1000000000ULL;

    printf("Simulating 10Mbps stream...\n");
    for (uint64_t i = 0; i < pkts_to_send; i++) {
        uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x10};
        if (i % 20 == 0) {  // PCR every 20 packets (~30ms at 10Mbps)
            uint64_t pcr_ticks = (i * 188ULL * 8 * 27000000ULL) / target_bitrate;
            encode_pcr(pkt, pcr_ticks);
        }
        tsa_process_packet(h, pkt, now_ns);
        now_ns += interval_ns;
    }

    tsa_commit_snapshot(h, now_ns);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("Results:\n");
    printf("  Physical Bitrate: %lu bps\n", (unsigned long)snap.stats.physical_bitrate_bps);
    printf("  PCR Bitrate:      %lu bps\n", (unsigned long)snap.stats.pcr_bitrate_bps);

    // Check if within 5% error
    double phys_err = fabs((double)snap.stats.physical_bitrate_bps - target_bitrate) / target_bitrate;
    double pcr_err = fabs((double)snap.stats.pcr_bitrate_bps - target_bitrate) / target_bitrate;

    printf("  Phys Error: %.2f%%\n", phys_err * 100.0);
    printf("  PCR Error:  %.2f%%\n", pcr_err * 100.0);

    assert(phys_err < 0.05);
    assert(pcr_err < 0.05);

    tsa_destroy(h);
    printf("Test passed.\n");
    return 0;
}
