#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa_bitstream.h"

void test_pcr_extraction() {
    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[3] = 0x30;  // Payload only, adaptation field = 3 (both)
    pkt[4] = 7;     // Adaptation field length
    pkt[5] = 0x10;  // PCR flag

    /* PCR = base * 300 + ext */
    /* Let's set PCR = 300 (base = 1, ext = 0) */
    pkt[6] = 0x00;
    pkt[7] = 0x00;
    pkt[8] = 0x00;
    pkt[9] = 0x00;
    pkt[10] = 0x80;  // base bit 0 is at pkt[10] bit 7
    pkt[11] = 0x00;  // ext

    uint64_t pcr = tsa_pkt_get_pcr(pkt);
    assert(pcr == 300);

    /* Test PCR = 27,000,000 (1 second) */
    /* 27,000,000 = 90,000 * 300 + 0 */
    /* 90,000 = 0x15F90 */
    /* binary: 000000000 000101011 11111001 00000000 0 */
    /* PCR Base (33 bits):
       00000000 00001010 11111110 01000000 0
       pkt[6]: 00000000 (0x00)
       pkt[7]: 00001010 (0x0A)
       pkt[8]: 11111110 (0xFE)
       pkt[9]: 01000000 (0x40)
       pkt[10]: 0 (bit 7)
    */
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[3] = 0x30;
    pkt[4] = 7;
    pkt[5] = 0x10;

    uint64_t target_base = 90000;
    pkt[6] = (target_base >> 25) & 0xFF;
    pkt[7] = (target_base >> 17) & 0xFF;
    pkt[8] = (target_base >> 9) & 0xFF;
    pkt[9] = (target_base >> 1) & 0xFF;
    pkt[10] = (target_base << 7) & 0x80;

    pcr = tsa_pkt_get_pcr(pkt);
    assert(pcr == 27000000);

    printf("test_pcr_extraction passed.\n");
}

void test_pcr_to_ns() {
    assert(tsa_pcr_to_ns(27000000) == 1000000000ULL);
    assert(tsa_pcr_to_ns(0) == 0);
    assert(tsa_pcr_to_ns(INVALID_PCR) == 0);

    printf("test_pcr_to_ns passed.\n");
}

int main() {
    test_pcr_extraction();
    test_pcr_to_ns();
    printf("All PCR bitstream tests passed.\n");
    return 0;
}
