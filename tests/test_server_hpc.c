#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tsa.h"

// Mock stream node for testing core logic of tsa_server.c
typedef struct {
    uint8_t remainder[188];
    int remainder_len;
    tsa_handle_t *tsa;
} mock_stream_node_t;

static void process_buffer_stateful(mock_stream_node_t *s, uint8_t *buf, int len, uint64_t now) {
    int pos = 0;
    while (pos < len) {
        if (s->remainder_len > 0) {
            int need = 188 - s->remainder_len;
            int can_take = (len - pos < need) ? (len - pos) : need;
            memcpy(s->remainder + s->remainder_len, buf + pos, can_take);
            s->remainder_len += can_take;
            pos += can_take;
            if (s->remainder_len == 188) {
                if (s->remainder[0] == 0x47) tsa_process_packet(s->tsa, s->remainder, now);
                s->remainder_len = 0;
            }
            continue;
        }
        if (buf[pos] == 0x47) {
            if (pos + 188 <= len) {
                tsa_process_packet(s->tsa, buf + pos, now);
                pos += 188;
            } else {
                s->remainder_len = len - pos;
                memcpy(s->remainder, buf + pos, s->remainder_len);
                pos = len;
            }
        } else {
            pos++;
        }
    }
}

void test_stateful_reassembly() {
    printf("Testing Stateful Reassembly... ");
    tsa_config_t cfg = {.is_live = true};
    tsa_handle_t *h = tsa_create(&cfg);
    mock_stream_node_t node = {.tsa = h, .remainder_len = 0};

    uint8_t full_pkt[188];
    memset(full_pkt, 0, 188);
    full_pkt[0] = 0x47;
    full_pkt[1] = 0x1F;
    full_pkt[2] = 0xFF;

    // Fragmented across UDP
    process_buffer_stateful(&node, full_pkt, 100, 1000);
    process_buffer_stateful(&node, full_pkt + 100, 88, 2000);

    // Sync offset in UDP
    uint8_t mixed[200];
    memset(mixed, 0, 200);
    mixed[10] = 0x47;  // Sync byte at index 10
    mixed[11] = 0x1F;
    process_buffer_stateful(&node, mixed, 200, 3000);

    tsa_commit_snapshot(h, 4000);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    // total_ts_packets should be at least 2
    assert(snap.stats.total_ts_packets >= 2);
    printf("[PASS]\n");
    tsa_destroy(h);
}

void test_socket_options_path() {
    printf("Testing Socket Options Path (Busy Poll/Timestamping)... ");
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);

#ifdef SO_BUSY_POLL
    int val = 50;
    setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &val, sizeof(val));
#endif

#ifdef SO_TIMESTAMPING
    int ts_flags = 0x4F;
    setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &ts_flags, sizeof(ts_flags));
#endif

    close(fd);
    printf("[PASS]\n");
}

int main() {
    test_stateful_reassembly();
    test_socket_options_path();
    return 0;
}
