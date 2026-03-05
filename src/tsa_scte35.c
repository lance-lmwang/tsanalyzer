/**
 * @file tsa_scte35.c
 * @brief Detailed parsing of SCTE-35 Splice Information Sections.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa_internal.h"

static const char* scte35_command_name(uint8_t type) {
    switch (type) {
        case 0x00: return "Splice Null";
        case 0x04: return "Splice Schedule";
        case 0x05: return "Splice Insert";
        case 0x06: return "Time Signal";
        case 0x07: return "Bandwidth Reservation";
        case 0xff: return "Private Command";
        default: return "Reserved";
    }
}

void tsa_scte35_process(tsa_handle_t* h, uint16_t pid, const uint8_t* p, int len) {
    if (!h || !p || len < 11) return;
    if (p[0] != 0xFC) return; /* table_id check */

    int section_len = ((p[1] & 0x0F) << 8) | p[2];
    if (section_len + 3 > len) return;

    bit_reader_t r;
    br_init(&r, p, len);
    br_skip(&r, 3 * 8); /* Skip table_id and section_length */

    br_read(&r, 8);  /* protocol_version */
    br_read(&r, 1);  /* encrypted_packet */
    br_read(&r, 6);  /* encryption_algorithm */
    uint64_t pts_adjust = ((uint64_t)br_read(&r, 1) << 32) | br_read(&r, 32);

    br_read(&r, 8);  /* cw_index */
    br_read(&r, 12); /* tier */
    (void)br_read(&r, 12);
    uint8_t splice_command_type = br_read(&r, 8);

    const char* cmd_name = scte35_command_name(splice_command_type);

    printf("[%s] SCTE-35: 0x%04x | Command: %s (0x%02x) | Adj: %lu\n",
           h->config.input_label[0] ? h->config.input_label : "GLOBAL",
           pid, cmd_name, splice_command_type, pts_adjust);

    if (splice_command_type == 0x05) { /* Splice Insert */
        uint32_t event_id = br_read(&r, 32);
        bool cancel = br_read(&r, 1);
        br_read(&r, 7); /* Reserved */
        if (!cancel) {
            bool out_of_network = br_read(&r, 1);
            bool program_splice = br_read(&r, 1);
            bool duration_flag = br_read(&r, 1);
            bool immediate = br_read(&r, 1);
            br_read(&r, 4); /* Reserved */

            printf("  EventID: 0x%08x | Out-of-Net: %d | Program-Splice: %d | Immediate: %d\n",
                   event_id, out_of_network, program_splice, immediate);

            if (program_splice && !immediate) {
                if (br_read(&r, 1)) { /* time_specified_flag */
                    uint64_t pts_time = ((uint64_t)br_read(&r, 1) << 32) | br_read(&r, 32);
                    printf("  Scheduled PTS: %lu\n", pts_time);
                }
            }

            if (duration_flag) {
                br_read(&r, 6); /* auto_return (1), reserved (5) */
                uint64_t duration = ((uint64_t)br_read(&r, 1) << 32) | br_read(&r, 32);
                printf("  Break Duration: %.3f sec\n", (double)duration / 90000.0);
            }
        }
    } else if (splice_command_type == 0x06) { /* Time Signal */
        if (br_read(&r, 1)) { /* time_specified_flag */
            uint64_t pts_time = ((uint64_t)br_read(&r, 1) << 32) | br_read(&r, 32);
            printf("  Time Signal PTS: %lu\n", pts_time);
        }
    }

    tsa_push_event(h, TSA_EVENT_SCTE35, pid, splice_command_type);
}
