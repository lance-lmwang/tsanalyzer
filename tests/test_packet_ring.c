#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"

void test_basic_push_pop() {
    printf("Testing basic push/pop...\n");
    tsa_packet_ring_t* r = tsa_packet_ring_create(1024);
    assert(r != NULL);

    uint8_t pkt[188];
    memset(pkt, 0x47, 188);
    uint64_t ts = 123456789;

    int res __attribute__((unused)) = tsa_packet_ring_push(r, pkt, ts);
    assert(res == 0);

    uint8_t out_pkt[188];
    uint64_t out_ts;
    res = tsa_packet_ring_pop(r, out_pkt, &out_ts);
    assert(res == 0);
    assert(out_ts == ts);
    assert(out_pkt[0] == 0x47);
    assert(memcmp(pkt, out_pkt, 188) == 0);

    tsa_packet_ring_destroy(r);
    printf("Basic push/pop passed.\n");
}

void test_overflow() {
    printf("Testing overflow behavior...\n");
    tsa_packet_ring_t* r = tsa_packet_ring_create(4);  // Power of 2

    uint8_t pkt[188] = {0};
    for (int i = 0; i < 4; i++) {
        assert(tsa_packet_ring_push(r, pkt, (uint64_t)i) == 0);
    }
    // Should fail now
    assert(tsa_packet_ring_push(r, pkt, 4) != 0);

    tsa_packet_ring_destroy(r);
    printf("Overflow test passed.\n");
}

int main() {
    test_basic_push_pop();
    test_overflow();
    printf("All TSA Packet Ring tests passed!\n");
    return 0;
}
