#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsa_internal.h"

void test_writer_basic() {
    printf("Testing basic forensic writer...\n");
    tsa_packet_ring_t* ring = tsa_packet_ring_create(1024);
    const char* filename = "test_forensic.ts";

    tsa_forensic_writer_t* writer = tsa_forensic_writer_create(ring, filename);
    assert(writer != NULL);

    tsa_forensic_writer_start(writer);

    uint8_t pkt[188];
    memset(pkt, 0x47, 188);
    pkt[1] = 0x11;  // Dummy data

    for (int i = 0; i < 100; i++) {
        while (tsa_packet_ring_push(ring, pkt, (uint64_t)i) != 0) {
            usleep(100);
        }
    }

    // Wait for writer to drain
    usleep(100000);

    tsa_forensic_writer_stop(writer);
    tsa_forensic_writer_destroy(writer);

    // Verify file exists and has data
    FILE* f = fopen(filename, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    assert(size >= 100 * 188);

    unlink(filename);
    tsa_packet_ring_destroy(ring);
    printf("Basic writer test passed.\n");
}

int main() {
    test_writer_basic();
    printf("All Forensic Writer tests passed!\n");
    return 0;
}
