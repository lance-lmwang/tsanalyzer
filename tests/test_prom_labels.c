#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// We'll test a function that generates labels for a given PID
void tsa_export_pid_labels(tsa_metric_buffer_t* buf, tsa_handle_t* h, uint16_t pid);

void test_dynamic_label_injection() {
    printf("Running test_dynamic_label_injection...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Simulate PMT definition for a Video and Audio stream
    uint8_t pmt[32] = {0x02, 0xb0, 0x1d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1,
                       0x00, 0xf0, 0x00, 0x1b, 0xe1, 0x00, 0xf0, 0x00,  // Video (H.264) at PID 0x100
                       0x0f, 0xe1, 0x01, 0xf0, 0x00,                    // Audio (AAC) at PID 0x101
                       0x00, 0x00, 0x00, 0x00};
    uint32_t pmt_crc = mpegts_crc32(pmt, 22);
    pmt[22] = (pmt_crc >> 24) & 0xff;
    pmt[23] = (pmt_crc >> 16) & 0xff;
    pmt[24] = (pmt_crc >> 8) & 0xff;
    pmt[25] = pmt_crc & 0xff;

    uint8_t pmt_pkt[188] = {TS_SYNC_BYTE, 0x50, 0x00, 0x10, 0x00};  // PID 0x1000 + PUSI
    memcpy(pmt_pkt + 5, pmt, sizeof(pmt));

    uint8_t pat[16] = {0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t pat_crc = mpegts_crc32(pat, 12);
    pat[12] = (pat_crc >> 24) & 0xff;
    pat[13] = (pat_crc >> 16) & 0xff;
    pat[14] = (pat_crc >> 8) & 0xff;
    pat[15] = pat_crc & 0xff;
    uint8_t pat_pkt[188] = {TS_SYNC_BYTE, 0x40, 0x00, 0x10, 0x00};
    memcpy(pat_pkt + 5, pat, sizeof(pat));

    tsa_process_packet(h, pat_pkt, 1000000000ULL);
    tsa_process_packet(h, pmt_pkt, 1100000000ULL);

    // Process some packets to make PIDs active
    uint8_t v_pkt[188] = {TS_SYNC_BYTE, 0x01, 0x00, 0x10};
    uint8_t a_pkt[188] = {TS_SYNC_BYTE, 0x01, 0x01, 0x10};
    tsa_process_packet(h, v_pkt, 1200000000ULL);
    tsa_process_packet(h, a_pkt, 1200000000ULL);

    tsa_commit_snapshot(h, 2000000000ULL);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    char buffer[1024];
    tsa_metric_buffer_t buf;
    tsa_mbuf_init(&buf, buffer, sizeof(buffer));

    // Generate labels for Video PID 0x100
    tsa_export_pid_labels(&buf, h, 0x100);
    assert(buf.offset > 0);
    buffer[buf.offset] = '\0';

    // We expect the labels to correctly identify the type and codec
    // Format: pid="0x0100",type="Video",codec="H.264"
    assert(strstr(buffer, "pid=\"0x0100\"") != NULL);
    assert(strstr(buffer, "type=\"Video\"") != NULL);
    assert(strstr(buffer, "codec=\"H.264\"") != NULL);

    // Generate labels for Audio PID 0x101
    buf.offset = 0;  // Reset buffer
    tsa_export_pid_labels(&buf, h, 0x101);
    buffer[buf.offset] = '\0';
    assert(strstr(buffer, "pid=\"0x0101\"") != NULL);
    assert(strstr(buffer, "type=\"Audio\"") != NULL);
    assert(strstr(buffer, "codec=\"ADTS-AAC\"") != NULL);  // 0x0f is ADTS AAC

    tsa_destroy(h);
    printf("test_dynamic_label_injection passed.\n");
}

int main() {
    test_dynamic_label_injection();
    return 0;
}
