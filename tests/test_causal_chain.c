#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void encode_pcr(uint8_t* pkt, uint64_t pcr_ticks) {
    uint64_t base = pcr_ticks / 300;
    uint16_t ext = (uint16_t)(pcr_ticks % 300);
    pkt[3] = (pkt[3] & 0x0F) | 0x20;
    pkt[4] = 7;
    pkt[5] = 0x10;
    pkt[6] = (uint8_t)(base >> 25);
    pkt[7] = (uint8_t)(base >> 17);
    pkt[8] = (uint8_t)(base >> 9);
    pkt[9] = (uint8_t)(base >> 1);
    pkt[10] = (uint8_t)((base << 7) | 0x7E | (ext >> 8));
    pkt[11] = (uint8_t)(ext & 0xFF);
}

int main() {
    tsa_config_t cfg = {0};
    cfg.is_live = true;
    tsa_handle_t* h = tsa_create(&cfg);

    uint64_t bitrate = 10000000;
    uint64_t interval = (188ULL * 8 * 1000000000ULL) / bitrate;
    uint64_t now = h->start_ns;
    uint8_t cc = 0;

    printf("1. Stable Phase (10Mbps)\n");
    for (int i = 0; i < 5000; i++) {
        uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x10};
        pkt[3] |= (cc & 0x0F);
        if (i % 20 == 0) encode_pcr(pkt, (i * 188ULL * 8 * 27000000ULL) / bitrate);
        tsa_process_packet(h, pkt, now);
        now += interval;
        cc = (cc + 1) & 0x0F;
    }
    h->seen_pat = h->seen_pmt = true;
    h->last_pat_ns = h->last_pmt_ns = now;
    h->pid_eb_fill_q64[0x0100] = INT_TO_Q64_64(50000000);
    h->stc_drift_slope = 1.0;

    tsa_commit_snapshot(h, now);
    tsa_snapshot_full_t s1;
    tsa_take_snapshot_full(h, &s1);
    printf("   Health: %.1f, RST Net: %.1f, RST Enc: %.1f, MDI-DF: %.1f\n", s1.summary.master_health,
           s1.predictive.rst_network_s, s1.predictive.rst_encoder_s, s1.stats.mdi_df_ms);
    assert(s1.summary.master_health > 95.0);

    printf("2. Jitter Phase (Inject 45ms PCR jitter)\n");
    // We want MDI-DF > 40ms (80% of 50ms)
    for (int i = 0; i < 5000; i++) {
        uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x10};
        pkt[3] |= (cc & 0x0F);
        if (i % 20 == 0) {
            uint64_t pcr_ticks = ((5000 + (uint64_t)i) * 188ULL * 8 * 27000000ULL) / bitrate;
            // Subtract 45ms from PCR to simulate network delay (Local - PCR increases)
            if (i % 40 == 0) pcr_ticks -= (45 * 27000);
            encode_pcr(pkt, pcr_ticks);
        }
        tsa_process_packet(h, pkt, now);
        now += interval;
        cc = (cc + 1) & 0x0F;
    }
    h->last_pat_ns = h->last_pmt_ns = now;
    h->pid_eb_fill_q64[0x0100] = INT_TO_Q64_64(50000000);
    h->stc_drift_slope = 1.0;
    tsa_commit_snapshot(h, now);
    tsa_take_snapshot_full(h, &s1);
    printf("   Health: %.1f, RST Net: %.1f, MDI-DF: %.1f\n", s1.summary.master_health, s1.predictive.rst_network_s,
           s1.stats.mdi_df_ms);
    assert(s1.stats.mdi_df_ms > 40.0);
    // 100 - 15 = 85.
    assert(s1.summary.master_health >= 85.0 && s1.summary.master_health < 90.0);

    printf("3. Fatal Loss Phase (Drop Physical In)\n");
    for (int i = 0; i < 2500; i++) {
        uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x10};
        pkt[3] |= (cc & 0x0F);
        if (i % 20 == 0) encode_pcr(pkt, ((10000 + (uint64_t)i) * 188ULL * 8 * 27000000ULL) / bitrate);
        tsa_process_packet(h, pkt, now);
        now += interval * 2;
        cc = (cc + 1) & 0x0F;
    }
    h->last_pat_ns = h->last_pmt_ns = now;
    h->pid_eb_fill_q64[0x0100] = INT_TO_Q64_64(50000000);
    h->stc_drift_slope = 1.0;
    tsa_commit_snapshot(h, now);
    tsa_take_snapshot_full(h, &s1);
    printf("   Health: %.1f, RST Net: %.1f, MDI-DF: %.1f\n", s1.summary.master_health, s1.predictive.rst_network_s,
           s1.stats.mdi_df_ms);
    assert(s1.predictive.rst_network_s < 5.0);
    assert(s1.summary.master_health <= 60.0);
    assert(s1.summary.lid_active);

    tsa_destroy(h);
    printf("Causal chain verified.\n");
    return 0;
}
