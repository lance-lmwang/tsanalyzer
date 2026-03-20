#include "libtsshaper.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#define TS_PACKET_SIZE 188

void test_basic_cbr() {
    printf("Testing Basic CBR...\n");
    uint64_t bitrate = 50000000; // 50Mbps
    tsa_shaper_t* shaper = tsa_shaper_create(bitrate);
    assert(shaper != NULL);

    tsa_shaper_add_program(shaper, 1);

    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x00; // PID 0
    pkt[2] = 0x01;
    pkt[3] = 0x10;

    // Push 5000 packets
    int pushed = 0;
    for (int i = 0; i < 5000; i++) {
        int res = tsa_shaper_push(shaper, 1, pkt);
        if (res == 0) pushed++;
        else if (res == -2) {
            // Backpressure, wait a bit
            usleep(100);
            i--;
        }
    }

    printf("Pushed %d packets.\n", pushed);
    usleep(100000); // 100ms

    tsa_shaper_stats_t stats;
    tsa_shaper_get_stats(shaper, &stats);
    printf("Sent %lu bytes, Avg Jitter: %.2f ns\n", stats.bytes_sent, stats.pcr_jitter_ns);

    assert(stats.bytes_sent > 0);

    tsa_shaper_destroy(shaper);
    printf("Basic CBR Passed.\n\n");
}

void test_pcr_accuracy() {
    printf("Testing PCR Accuracy...\n");
    uint64_t bitrate = 20000000; // 20Mbps
    tsa_shaper_t* shaper = tsa_shaper_create(bitrate);
    tsa_shaper_add_program(shaper, 1);

    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x01;
    pkt[2] = 0x64; // PID 100
    pkt[3] = 0x30; // AF + Payload
    pkt[4] = 0x07; // AF length
    pkt[5] = 0x10; // PCR flag

    for (int i = 0; i < 2000; i++) {
        while (tsa_shaper_push(shaper, 1, pkt) != 0) {
            usleep(500);
        }
    }

    usleep(200000); // Wait for some packets to clear

    tsa_shaper_stats_t stats;
    tsa_shaper_get_stats(shaper, &stats);
    printf("PCR Jitter: %.2f ns (count %lu)\n", stats.pcr_jitter_ns, stats.bytes_sent / TS_PACKET_SIZE);
    // On a VM or shared host, jitter might be higher, but let's check for reasonable < 1ms
    assert(stats.pcr_jitter_ns < 1000000);

    tsa_shaper_destroy(shaper);
    printf("PCR Accuracy Passed.\n\n");
}

void test_backpressure() {
    printf("Testing Backpressure (HWM)...\n");
    uint64_t bitrate = 1000000; // 1Mbps (very slow)
    tsa_shaper_t* shaper = tsa_shaper_create(bitrate);
    tsa_shaper_add_program(shaper, 1);

    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0xFF, TS_PACKET_SIZE);
    pkt[0] = 0x47;
    pkt[1] = 0x00;
    pkt[2] = 0x00; // PID 0
    pkt[3] = 0x10;

    int push_count = 0;
    int backpressure_hit = 0;
    for (int i = 0; i < 100000; i++) {
        int res = tsa_shaper_push(shaper, 1, pkt);
        if (res == -2) {
            backpressure_hit = 1;
            break;
        }
        if (res == 0) push_count++;
        else break;
    }

    printf("Pushed %d packets before backpressure\n", push_count);
    assert(backpressure_hit == 1);

    tsa_shaper_destroy(shaper);
    printf("Backpressure Passed.\n\n");
}

int main() {
    printf("Starting LibTSShaper Test Suite...\n\n");
    test_basic_cbr();
    test_pcr_accuracy();
    test_backpressure();
    printf("All tests passed successfully!\n");
    return 0;
}
