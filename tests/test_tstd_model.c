#include "tsa_internal.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_tstd_full_pipeline() {
    printf("Running test_tstd_full_pipeline...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188] = {0};
    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = 0x01;  // PID 0x100
    pkt[2] = 0x00;
    pkt[3] = TS_PAYLOAD_FLAG;
    h->live->pid_is_referenced[0x100] = true;  // Mark as elementary stream

    // 1. Initial Packet
    uint64_t now_ns = 1000000000ULL;
    tsa_process_packet(h, pkt, now_ns);

    uint32_t tb = tsa_get_pid_tb_fill(h, 0x100);
    uint32_t mb = tsa_get_pid_mb_fill(h, 0x100);
    uint32_t eb = tsa_get_pid_eb_fill(h, 0x100);
    printf("Initial: TB=%u, MB=%u, EB=%u\n", tb, mb, eb);
    assert(tb == 188);
    assert(mb == 184);
    assert(eb == 184);

    // 2. After 10ms (Wait less time so EB doesn't empty)
    tsa_process_packet(h, pkt, now_ns + 10000000ULL);
    tb = tsa_get_pid_tb_fill(h, 0x100);
    mb = tsa_get_pid_mb_fill(h, 0x100);
    eb = tsa_get_pid_eb_fill(h, 0x100);
    printf("After 10ms: TB=%u, MB=%u, EB=%u\n", tb, mb, eb);
    assert(tb == 188);
    assert(mb == 184);
    // 10ms leak at 40Mbps (0.8*50) = 50,000 bytes.
    // Still empties it. We need a very fast burst to see EB accumulation.

    // 3. Burst: 100 packets
    for (int i = 0; i < 100; i++) {
        tsa_process_packet(h, pkt, now_ns + 20000000ULL + i * 1000);  // 1us gap
    }
    eb = tsa_get_pid_eb_fill(h, 0x100);
    printf("After burst: EB=%u\n", eb);
    assert(eb > 10000);

    // 3. PES Start (Trigger EB drain)
    pkt[1] |= 0x40;  // Payload unit start indicator
    tsa_process_packet(h, pkt, now_ns + 200000000ULL);
    eb = tsa_get_pid_eb_fill(h, 0x100);
    printf("After PES Start: EB=%u\n", eb);
    // Since we drain 100KB on PES start, it should be 0 (clipped)
    assert(eb == 0);

    tsa_destroy(h);
    printf("test_tstd_full_pipeline passed.\n");
}

int main() {
    test_tstd_full_pipeline();
    return 0;
}
