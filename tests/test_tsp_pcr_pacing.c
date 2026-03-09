#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tsp.h"

/* Helper to create a PCR packet */
void create_pcr_pkt(uint8_t* buf, uint64_t pcr_ticks) {
    memset(buf, 0, 188);
    buf[0] = 0x47;
    buf[1] = 0x01;
    buf[2] = 0x00;  // PID 0x100
    buf[3] = 0x20;  // AF
    buf[4] = 7;
    buf[5] = 0x10;  // PCR flag
    uint64_t base = pcr_ticks / 300;
    uint16_t ext = pcr_ticks % 300;
    buf[6] = (base >> 25) & 0xFF;
    buf[7] = (base >> 17) & 0xFF;
    buf[8] = (base >> 9) & 0xFF;
    buf[9] = (base >> 1) & 0xFF;
    buf[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    buf[11] = ext & 0xFF;
}

int main() {
    printf(">>> Testing TSP Pacer: PCR-to-Wall-Clock Synchronization...\n");

    tsp_config_t cfg = {0};
    cfg.mode = TSPACER_MODE_PCR;
    cfg.pcr_pid = 0x100;
    /* We intentionally set a WRONG bitrate (20Mbps)
     * but the file PCRs will be at 10Mbps.
     * A PCR-driven pacer MUST follow the PCR, not this 20Mbps config. */
    cfg.bitrate = 20000000;
    tsp_handle_t* h = tsp_create(&cfg);
    assert(h);

    uint8_t pkts[188 * 10];
    uint64_t pcr = 27000000ULL;

    /* Simulate 10 PCR packets, each 100ms apart in PCR domain (10Mbps rate) */
    /* 100ms at 10Mbps = 1,000,000 bits = 125,000 bytes = ~665 TS packets */
    uint64_t pcr_step = 27000 * 100;  // 100ms

    printf("[STEP 1] Enqueueing PCR-timed packets...\n");
    uint64_t t_prev = 0;
    for (int i = 0; i < 10; i++) {
        create_pcr_pkt(pkts, pcr);
        tsp_enqueue(h, pkts, 1);

        extern uint64_t tsp_get_total_packets_queued(tsp_handle_t * h);
        extern uint64_t tsp_debug_get_scheduled_time(tsp_handle_t * h, int idx);

        uint64_t total = tsp_get_total_packets_queued(h);
        uint64_t t_now = tsp_debug_get_scheduled_time(h, total - 1);

        if (i > 0) {
            double diff_ms = (double)(t_now - t_prev) / 1000000.0;
            printf("   PCR %d scheduled at +%.2f ms\n", i, diff_ms);
            /* Gap should be ~100ms regardless of the 20Mbps bitrate setting */
            if (diff_ms < 90.0 || diff_ms > 110.0) {
                printf("[FAIL] PCR packet %d scheduled at wrong time: %.2f ms\n", i, diff_ms);
                tsp_destroy(h);
                return 1;
            }
        }
        t_prev = t_now;
        pcr += pcr_step;
    }

    printf("[PASS] Pacer correctly followed PCR timing.\n");
    tsp_destroy(h);
    return 0;
}
