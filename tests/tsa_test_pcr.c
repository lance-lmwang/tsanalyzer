#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa_bitstream.h"
#include "tsa_internal.h"

// Include source to test static function
#include "../src/tsp.c"

void test_pcr_extraction() {
    uint8_t pkt[188] = {0};
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[3] = 0x20;  // AFC = 2 (AF only)
    pkt[4] = 7;     // AF length
    pkt[5] = 0x10;  // PCR flag

    // Mock PCR: base=1, ext=0 -> value = 300
    pkt[6] = 0;
    pkt[7] = 0;
    pkt[8] = 0;
    pkt[9] = 0;
    pkt[10] = 0x80;
    pkt[11] = 0;

    uint64_t pcr = tsa_pkt_get_pcr(pkt);
    printf("Extracted PCR: %lu\n", pcr);
    assert(pcr == 300);

    // Mock PCR: base=0x1FFFFFFFF, ext=299
    pkt[6] = 0xFF;
    pkt[7] = 0xFF;
    pkt[8] = 0xFF;
    pkt[9] = 0xFF;
    pkt[10] = 0x81;
    pkt[11] = 0x2B;
    pcr = tsa_pkt_get_pcr(pkt);
    printf("Extracted PCR (large): %lu\n", pcr);
    uint64_t expected = (0x1FFFFFFFFULL * 300) + 299;
    (void)expected;
    assert(pcr == expected);

    printf("PCR extraction tests passed!\n");
}

int main() {
    test_pcr_extraction();
    return 0;
}
