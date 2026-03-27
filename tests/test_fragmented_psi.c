#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

int main() {
    printf(">>> STARTING FRAGMENTED PSI SECTION TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t pid = 0x00;  // PAT
    ts_decode_result_t res = {0};
    res.pid = pid;
    res.has_payload = true;
    res.payload_len = 184;

    /* Construct Packet 1: Start of PAT, but truncated */
    uint8_t pkt1[188];
    memset(pkt1, 0xFF, 188);
    pkt1[0] = 0x47;
    pkt1[1] = 0x40;  // PUSI=1
    pkt1[2] = 0x00;
    pkt1[3] = 0x10;  // CC=0
    pkt1[4] = 0x00;  // Pointer Field
    pkt1[5] = 0x00;  // TID=0 (PAT)
    pkt1[6] = 0xB0;  // Syntax=1
    pkt1[7] = 0x0D;  // Length=13 (Short!)
    // ... data ...

    tsa_decode_packet(h, pkt1, 1000, &res);
    tsa_section_filter_push(h, pid, pkt1, &res);

    assert(h->pid_filters[pid] != NULL && h->pid_filters[pid]->active == true);
    printf("Packet 1 processed (Section pending)\n");

    /* Construct Packet 2: Tail of Section 1 AND Start of Section 2
     * Using Pointer Field = 5 (5 bytes are for the old section) */
    uint8_t pkt2[188];
    memset(pkt2, 0xFF, 188);
    pkt2[0] = 0x47;
    pkt2[1] = 0x40;  // PUSI=1
    pkt2[2] = 0x00;
    pkt2[3] = 0x11;  // CC=1
    pkt2[4] = 0x05;  // Pointer Field = 5
    // Byte 5-9: Tail of old section (must trigger completion)
    // Byte 10: TID of new section

    tsa_decode_packet(h, pkt2, 2000, &res);
    tsa_section_filter_push(h, pid, pkt2, &res);

    /* If logic is correct, Section 1 should have been parsed
     * and Section 2 should now be active. */
    printf("Packet 2 processed (Multi-section fragmented handling)\n");
    assert(h->seen_pat == true);

    tsa_destroy(h);
    printf(">>> FRAGMENTED PSI SECTION TEST PASSED <<<\n");
    return 0;
}
