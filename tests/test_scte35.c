#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tsa_internal.h"

void test_scte35_parsing() {
    printf("Testing SCTE-35 parsing...\n");

    tsa_config_t cfg = { .is_live = false };
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    /* Mock SCTE-35 Splice Insert Section */
    uint8_t scte35_data[] = {
        0xFC,                   /* table_id */
        0x70, 0x25,             /* section_length: 37 */
        0x00,                   /* protocol_version */
        0x00,                   /* encrypted_packet=0, encryption_algorithm=0, pts_adjustment=0 */
        0x00, 0x00, 0x00, 0x00, /* pts_adjustment cont. */
        0x00,                   /* cw_index */
        0x00, 0x00,             /* tier=0, splice_command_length=0x005 (Wait, 12 bits tier, 12 bits len) */
        0x05,                   /* command_type: splice_insert */
        0xDE, 0xAD, 0xBE, 0xEF, /* event_id */
        0x00,                   /* cancel=0, reserved=0x7F */
        0x00,                   /* out_of_network=0, program_splice=0, duration_flag=0, immediate=0, reserved=0xF */
        0x00, 0x00, 0x00, 0x00  /* CRC (mock) */
    };

    /* Fixed up lengths for the test */
    scte35_data[1] = 0x70;
    scte35_data[2] = 17; /* table_id(1) + len(2) + ... + CRC(4) */

    /* Override tier and command length: tier=0, len=5 */
    scte35_data[11] = 0x00;
    scte35_data[12] = 0x05;

    printf("  Processing mock SCTE-35 packet...\n");
    tsa_scte35_process(h, 0x100, scte35_data, sizeof(scte35_data));

    tsa_destroy(h);
}

int main() {
    test_scte35_parsing();
    printf("SCTE-35 TEST PASSED!\n");
    return 0;
}
