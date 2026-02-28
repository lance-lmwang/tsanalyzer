#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_tstd_reset_on_res_change() {
    printf("Running test_tstd_reset_on_res_change...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // 1. Initial 1920x1080 SPS
    uint8_t sps1080[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x28, 0xd9, 0x00, 0x78, 0x02, 0x27, 0xe5, 0x84,
                         0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf2, 0x3c, 0x60, 0xc6, 0x58};
    uint8_t pes[] = {0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x80, 0x80, 0x05, 0x21, 0x00, 0x01, 0x00, 0x01};
    uint8_t pkt[188] = {TS_SYNC_BYTE, 0x41, 0x00, 0x10};  // PID 0x100 + PUSI
    memcpy(pkt + 4, pes, sizeof(pes));
    memcpy(pkt + 4 + sizeof(pes), sps1080, sizeof(sps1080));

    // Register PID 0x100 as H.264
    uint8_t pmt[21] = {0x02, 0xb0, 0x12, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x00, 0xf0,
                       0x00, 0x1b, 0xe1, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t pmt_crc = mpegts_crc32(pmt, 17);
    pmt[17] = (pmt_crc >> 24);
    pmt[18] = (pmt_crc >> 16);
    pmt[19] = (pmt_crc >> 8);
    pmt[20] = pmt_crc;
    uint8_t pmt_pkt[188] = {TS_SYNC_BYTE, 0x50, 0x00, 0x10, 0x00};  // PID 0x1000 + PUSI
    memcpy(pmt_pkt + 5, pmt, sizeof(pmt));

    uint8_t pat[16] = {0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t pat_crc = mpegts_crc32(pat, 12);
    pat[12] = (pat_crc >> 24);
    pat[13] = (pat_crc >> 16);
    pat[14] = (pat_crc >> 8);
    pat[15] = pat_crc;
    uint8_t pat_pkt[188] = {TS_SYNC_BYTE, 0x40, 0x00, 0x10, 0x00};
    memcpy(pat_pkt + 5, pat, sizeof(pat));

    tsa_process_packet(h, pat_pkt, 1000000000ULL);
    tsa_process_packet(h, pmt_pkt, 1100000000ULL);
    tsa_process_packet(h, pkt, 1200000000ULL);

    assert(tsa_get_pid_tb_fill(h, 0x100) > 0);

    // 2. Change SPS to Baseline 720p (actually 320x180 in this specific small SPS)
    uint8_t sps720[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f, 0x8d, 0x8d, 0x40, 0xa0, 0x2b, 0xf1};
    memcpy(pkt + 4 + sizeof(pes), sps720, sizeof(sps720));
    tsa_process_packet(h, pkt, 1300000000ULL);

    // Flush the second PES packet
    uint8_t flush_pkt[188] = {TS_SYNC_BYTE, 0x41, 0x00, 0x10};
    tsa_process_packet(h, flush_pkt, 1400000000ULL);

    uint32_t fill = tsa_get_pid_tb_fill(h, 0x100);
    printf("TB Fill after resolution change: %u\n", fill);

    tsa_commit_snapshot(h, 1800000000ULL);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    printf("Updated Width: %u\n", snap.pids[0x100].width);

    assert(snap.pids[0x100].width != 1920);  // Should have changed
    assert(fill == 188);                     // Should have reset to 0 then added current packet (188)

    tsa_destroy(h);
    printf("test_tstd_reset_on_res_change passed.\n");
}

int main() {
    test_tstd_reset_on_res_change();
    return 0;
}
