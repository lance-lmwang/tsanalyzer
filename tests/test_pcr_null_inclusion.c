#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "tsa_pcr_track.h"
#include "tsa_units.h"

int main() {
    printf(">>> STARTING PCR NULL PACKET INCLUSION UNIT TEST <<<\n");

    tsa_pcr_track_t track;
    tsa_pcr_track_init(&track, 0x100, 1);

    uint64_t pcr1 = 1000000;
    uint64_t arrival1 = 0;
    uint64_t pid_pkts = 1;
    uint64_t total_pkts = 1;

    // 1. Initial PCR
    tsa_pcr_track_update(&track, pcr1, arrival1, pid_pkts, total_pkts, false);

    // 2. Simulate 1000 packets total, where only 10 are from our PID (rest are 0x1FFF)
    // We only call update on our PID packets, but the total_pkts should reflect reality.
    uint64_t pcr2 = pcr1 + (27000000ULL / 10);  // 100ms later in PCR time
    uint64_t arrival2 = 100000000ULL;           // 100ms later in arrival time
    uint64_t next_pid_pkts = 10;
    uint64_t next_total_pkts = 1000;  // 990 Null packets passed through the engine!

    tsa_pcr_track_update(&track, pcr2, arrival2, next_pid_pkts, next_total_pkts, true);

    // 3. Expected Mux Bitrate:
    // Bits = (1000 - 1) * (double)TS_PACKET_SIZE * 8 = 1,498,496 bits
    // Time = 0.1s
    // Rate = 14,984,960 bps (~14.98 Mbps)

    printf("   Calculated MUX Bitrate: %lu bps\n", (unsigned long)track.mux_bitrate_bps);
    printf("   Calculated PID Bitrate: %lu bps\n", (unsigned long)track.bitrate_bps);

    // If we ignored null packets, mux_bitrate would be ~ (9 * (double)TS_PACKET_SIZE * 8 / 0.1) = 135,360 bps.
    // 14.9 Mbps vs 0.13 Mbps is a massive difference.
    assert(track.mux_bitrate_bps > 14000000);
    assert(track.bitrate_bps < 200000);

    printf("[PASS] PCR Bitrate correctly includes Null packets in MUX total.\n");
    return 0;
}
