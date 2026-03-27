#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/core/internal.h"
#include "tsshaper/tsshaper.h"

#define TS_PACKET_SIZE 188

// Minimal Broadcast Grade PSI Generator (PAT/PMT) with CC tracking
void inject_psi(tsshaper_t* shaper, uint16_t v_pid, uint16_t a_pid, uint64_t ts) {
    static uint8_t pat_cc = 0;
    static uint8_t pmt_cc = 0;

    uint8_t pat[TS_PACKET_SIZE];
    memset(pat, 0xFF, sizeof(pat));
    pat[0] = 0x47;
    pat[1] = 0x40;
    pat[2] = 0x00;
    pat[3] = 0x10 | (pat_cc & 0x0F);
    pat_cc = (pat_cc + 1) & 0x0F;
    pat[4] = 0x00;
    uint8_t pat_body[] = {0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xe1, 0x00};
    memcpy(pat + 5, pat_body, sizeof(pat_body));
    tsshaper_push(shaper, 0x0000, pat, ts);

    uint8_t pmt[TS_PACKET_SIZE];
    memset(pmt, 0xFF, sizeof(pmt));
    pmt[0] = 0x47;
    pmt[1] = 0x50;
    pmt[2] = 0x00;
    pmt[3] = 0x10 | (pmt_cc & 0x0F);
    pmt_cc = (pmt_cc + 1) & 0x0F;
    pmt[4] = 0x00;
    uint8_t pmt_body[] = {0x02,
                          0xb0,
                          0x17,
                          0x00,
                          0x01,
                          0xc1,
                          0x00,
                          0x00,
                          (uint8_t)(0xe0 | (v_pid >> 8)),
                          (uint8_t)(v_pid & 0xFF),
                          0x00,
                          0x00,
                          0x1b,
                          (uint8_t)(0xe0 | (v_pid >> 8)),
                          (uint8_t)(v_pid & 0xFF),
                          0x00,
                          0x00,
                          0x0f,
                          (uint8_t)(0xe0 | (a_pid >> 8)),
                          (uint8_t)(a_pid & 0xFF),
                          0x00,
                          0x00};
    memcpy(pmt + 5, pmt_body, sizeof(pmt_body));
    tsshaper_push(shaper, 0x1000, pmt, ts);
}

#include <time.h>

int main(int argc, char** argv) {
    if (argc < 8) {
        fprintf(stderr, "Usage: %s <in.ts> <out.ts> <total_bps> <v_pid> <v_bps> <a_pid> <a_bps>\n", argv[0]);
        return 1;
    }

    const char* in_path = argv[1];
    const char* out_path = argv[2];
    uint64_t total_bitrate = strtoull(argv[3], NULL, 10);
    uint16_t v_pid = (uint16_t)strtol(argv[4], NULL, 0);
    uint64_t v_bitrate = strtoull(argv[5], NULL, 10);
    uint16_t a_pid = (uint16_t)strtol(argv[6], NULL, 0);
    uint64_t a_bitrate = strtoull(argv[7], NULL, 10);

    FILE* in_f = fopen(in_path, "rb");
    if (!in_f) {
        perror("fopen input");
        return 1;
    }
    FILE* out_f = fopen(out_path, "wb");
    if (!out_f) {
        perror("fopen output");
        return 1;
    }

    tsshaper_config_t config = {0};
    config.bitrate_bps = total_bitrate;
    tsshaper_t* shaper = tsshaper_create(&config);

    tsshaper_set_pid_bitrate(shaper, v_pid, v_bitrate);
    tsshaper_set_pid_bitrate(shaper, a_pid, a_bitrate);
    tsshaper_set_pid_bitrate(shaper, 0x0000, 15000);  // 15kbps for PAT
    tsshaper_set_pid_bitrate(shaper, 0x1000, 15000);  // 15kbps for PMT

    struct timespec ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    uint64_t virtual_now_ns = (uint64_t)ts_now.tv_sec * 1000000000ULL + ts_now.tv_nsec;

    // Force initialize the PCR generator for deterministic zero-jitter start
    shaper->master_pcr_pid = v_pid;
    shaper->start_pcr_base = 100000;  // Must be non-zero to prevent overwrite from late original PCR
    shaper->start_time_ns = virtual_now_ns;
    shaper->ideal_packet_time_ns = virtual_now_ns;
    shaper->next_packet_time_ns = virtual_now_ns;

    uint64_t total_packets_to_pull = total_bitrate * 5 / 8 / TS_PACKET_SIZE;
    uint64_t pulled = 0;
    uint8_t pending_pkt[TS_PACKET_SIZE];
    bool has_pending = false;

    uint32_t psi_timer = 0;

    while (pulled < total_packets_to_pull) {
        // Periodically inject PSI (~100ms)
        if (++psi_timer > (total_bitrate / 8 / TS_PACKET_SIZE / 10)) {
            inject_psi(shaper, v_pid, a_pid, virtual_now_ns);
            psi_timer = 0;
        }

        // 1. Fill the T-STD buffers until backpressure is reached
        while (1) {
            if (!has_pending) {
                if (fread(pending_pkt, 1, TS_PACKET_SIZE, in_f) != TS_PACKET_SIZE) {
                    fseek(in_f, 0, SEEK_SET);
                    fread(pending_pkt, 1, TS_PACKET_SIZE, in_f);
                }

                uint16_t pid = ((pending_pkt[1] & 0x1F) << 8) | pending_pkt[2];
                // STRIP SOURCE PADDING
                if (pid == 0x1FFF) continue;

                has_pending = true;
            }

            uint16_t pid = ((pending_pkt[1] & 0x1F) << 8) | pending_pkt[2];
            if (tsshaper_push(shaper, pid, pending_pkt, virtual_now_ns) == 0) {
                // Successfully queued in T-STD buffer
                has_pending = false;
            } else {
                // Backpressure hit! (TBn is full). Wait for the next pull cycle.
                break;
            }
        }

        // 2. Extract exactly one shaped packet to advance the physical output grid
        uint8_t out_pkt[TS_PACKET_SIZE];
        tsshaper_pull(shaper, out_pkt);
        if (fwrite(out_pkt, 1, TS_PACKET_SIZE, out_f) != TS_PACKET_SIZE) break;

        pulled++;
        virtual_now_ns += (188ULL * 8 * 1000000000ULL) / total_bitrate;
    }

    fclose(in_f);
    fclose(out_f);
    tsshaper_destroy(shaper);
    return 0;
}
