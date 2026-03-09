#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// Simulate PMT reference
void mock_pmt_reference(tsa_handle_t* h, uint16_t pid, uint8_t stream_type) {
    h->live->pid_is_referenced[pid] = true;
    h->pid_seen[pid] = true;
    h->program_count = 1;
    h->programs[0].stream_count = 1;
    h->programs[0].streams[0].pid = pid;
    h->programs[0].streams[0].stream_type = stream_type;

    // Debug: verify mapping
    printf("Mocked PID %d as type 0x%02x (%s)\n", pid, stream_type, tsa_stream_type_to_str(stream_type));
}

void test_aac_parsing() {
    printf("Testing AAC ADTS parsing...\n");
    tsa_config_t cfg = {.op_mode = TSA_MODE_LIVE};
    tsa_handle_t* h = tsa_create(&cfg);
    uint16_t pid = 100;
    mock_pmt_reference(h, pid, 0x0F);  // AAC

    // 48000Hz, 2 channels
    // Sync(12) | ID(1) | Layer(2) | protection(1) | profile(2) | freq(4) | private(1) | channel(3)
    // 0x4C = 0100 1100 -> profile=1(LC), freq=3(48k), priv=0, chan=0...
    // Wait, chan=2 is needed. bits: 01 0011 0 010 -> 0100 1100 10... -> 0x4C 0x80
    uint8_t adts_header[] = {0xFF, 0xF1, 0x4C, 0x80, 0x00, 0x1F, 0xFC};

    tsa_handle_es_payload(h, pid, adts_header, sizeof(adts_header), 1000000);

    assert(h->pid_audio_sample_rate[pid] == 48000);
    assert(h->pid_audio_channels[pid] == 2);
    assert(h->pid_profile[pid] == 2);  // AAC LC (Object Type 2)

    tsa_destroy(h);
    printf("AAC OK.\n");
}

void test_mp2_parsing() {
    printf("Testing MP2 parsing...\n");
    tsa_handle_t* h = tsa_create(NULL);
    uint16_t pid = 101;
    mock_pmt_reference(h, pid, 0x03);  // MPEG1 Audio

    // MPEG Audio Header: FF FD 90 44
    uint8_t mp2_header[] = {0xFF, 0xFD, 0x90, 0x44};

    tsa_handle_es_payload(h, pid, mp2_header, sizeof(mp2_header), 2000000);

    assert(h->pid_audio_sample_rate[pid] == 44100);
    assert(h->pid_audio_channels[pid] == 2);
    assert(h->pid_profile[pid] == 2);  // Layer II

    tsa_destroy(h);
    printf("MP2 OK.\n");
}

void test_h264_sps() {
    printf("Testing H.264 SPS parsing...\n");
    tsa_handle_t* h = tsa_create(NULL);
    uint16_t pid = 200;
    mock_pmt_reference(h, pid, 0x1B);  // H.264

    // Simple 1920x1080 SPS
    uint8_t h264_sps[] = {0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x28, 0xAC, 0x2B,
                          0x40, 0x3C, 0x01, 0x13, 0xF2, 0xC0, 0x3C, 0x4D, 0x40};

    tsa_handle_es_payload(h, pid, h264_sps, sizeof(h264_sps), 3000000);

    assert(h->pid_width[pid] == 1920);
    assert(h->pid_height[pid] == 1080);

    tsa_destroy(h);
    printf("H.264 OK.\n");
}

void test_h265_sps() {
    printf("Testing H.265 SPS parsing...\n");
    tsa_handle_t* h = tsa_create(NULL);
    uint16_t pid = 201;
    mock_pmt_reference(h, pid, 0x24);  // HEVC

    // 1280x720 HEVC SPS
    uint8_t h265_sps[] = {0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03,
                          0x00, 0x80, 0x00, 0x00, 0x03, 0x00, 0x00, 0x3C, 0xA0, 0x08, 0x08,
                          0x0A, 0x05, 0x89, 0x08, 0xDA, 0x05, 0xC1, 0x2C, 0x1C, 0x10};

    tsa_handle_es_payload(h, pid, h265_sps, sizeof(h265_sps), 4000000);

    assert(h->pid_width[pid] > 0);

    tsa_destroy(h);
    printf("H.265 OK.\n");
}

int main() {
    test_aac_parsing();
    test_mp2_parsing();
    test_h264_sps();
    test_h265_sps();
    printf("All ES Deep Analysis tests PASSED!\n");
    return 0;
}
