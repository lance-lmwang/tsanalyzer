#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// Helper to create a PES packet with PTS/DTS
static void fill_pes_header(uint8_t* p, uint8_t stream_id, uint16_t pes_len, uint64_t pts, uint64_t dts) {
    p[0] = 0x00;
    p[1] = 0x00;
    p[2] = 0x01;
    p[3] = stream_id;
    p[4] = (pes_len >> 8) & 0xFF;
    p[5] = pes_len & 0xFF;
    p[6] = 0x84;  // Optional header follows, data alignment indicator
    p[7] = 0xC0;  // PTS and DTS present
    p[8] = 0x0A;  // Header data length (5 for PTS + 5 for DTS)

    // PTS
    p[9] = 0x31 | ((pts >> 29) & 0x0E);
    p[10] = (pts >> 22) & 0xFF;
    p[11] = 0x01 | ((pts >> 14) & 0xFE);
    p[12] = (pts >> 7) & 0xFF;
    p[13] = 0x01 | ((pts << 1) & 0xFE);

    // DTS
    p[14] = 0x11 | ((dts >> 29) & 0x0E);
    p[15] = (dts >> 22) & 0xFF;
    p[16] = 0x01 | ((dts >> 14) & 0xFE);
    p[17] = (dts >> 7) & 0xFF;
    p[18] = 0x01 | ((dts << 1) & 0xFE);
}

void test_tstd_dts_removal() {
    printf("Running test_tstd_dts_removal...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint64_t now_ns = 1000000000ULL;  // 1s
    h->engine_started = true;
    h->start_ns = now_ns;
    h->stc_ns = now_ns;
    h->last_snap_ns = now_ns;

    uint16_t pid = 0x100;
    h->es_tracks[pid].stream_type = 0x1b;  // H.264
    h->live->pid_is_referenced[pid] = true;

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = (pid >> 8) & 0x1F;
    pkt[2] = pid & 0xFF;

    // Establish PCR/STC
    uint64_t pcr_base = 90000;  // 1ms in 90kHz (or 27MHz/300)

    // Send PCR packet
    pkt[3] = 0x20;  // Adaptation field only
    pkt[4] = 183;
    pkt[5] = 0x10;  // PCR flag
    uint64_t pcr_val = pcr_base;
    pkt[6] = (pcr_val >> 25) & 0xFF;
    pkt[7] = (pcr_val >> 17) & 0xFF;
    pkt[8] = (pcr_val >> 9) & 0xFF;
    pkt[9] = (pcr_val >> 1) & 0xFF;
    pkt[10] = (pcr_val << 7) & 0x80;
    pkt[11] = 0x00;  // extension
    tsa_process_packet(h, pkt, now_ns);
    h->stc_ns = now_ns;

    // Send data packets with PES header
    pkt[3] = 0x30;  // AF + Payload
    pkt[4] = 7;     // AF length (to make room for PCR if we wanted, but let's just use it for padding)

    uint64_t pts = pcr_base + 90000 * 2;  // 2 seconds later (relative to pcr_base)
    uint64_t dts = pcr_base + 90000 * 1;  // 1 second later (relative to pcr_base)

    fill_pes_header(pkt + 12, 0xE0, 1000, pts, dts);
    pkt[1] |= 0x40;  // PUSI

    tsa_process_packet(h, pkt, now_ns + 1000000);  // 1ms later

    // Send another PUSI to trigger the processing of the first one
    pkt[1] |= 0x40;                                // PUSI
    pkt[3] = ((pkt[3] + 1) & 0x0F) | 0x30;         // increment CC
    tsa_process_packet(h, pkt, now_ns + 2000000);  // 2ms later

    h->stc_locked = false;
    h->stc_ns = now_ns + 2000000;

    uint32_t eb_initial = h->live->pid_eb_fill_bytes[pid];
    (void)eb_initial;
    assert(eb_initial > 0);

    // Now move time forward to just before DTS (1.5s absolute)
    h->stc_ns = now_ns + 500000000ULL;
    tsa_commit_snapshot(h, now_ns + 500000000ULL);
    uint32_t eb_mid = h->live->pid_eb_fill_bytes[pid];
    (void)eb_mid;
    assert(eb_mid == eb_initial);

    // Move time forward past DTS (2.1s absolute)
    h->stc_ns = now_ns + 1100000000ULL;
    tsa_commit_snapshot(h, now_ns + 1100000000ULL);
    uint32_t eb_after = h->live->pid_eb_fill_bytes[pid];
    (void)eb_after;
    assert(eb_after == 0);

    tsa_destroy(h);
    printf("test_tstd_dts_removal PASSED\n");
}

int main() {
    test_tstd_dts_removal();
    return 0;
}
