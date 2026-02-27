#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "tsa.h"

void send_packets(tsa_handle_t* h, uint16_t pid, int count, uint64_t* now_ns, uint64_t interval_ns) {
    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = (pid >> 8) & 0x1F;
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x10; // Payload only

    for (int i = 0; i < count; i++) {
        // Every 40 packets, add PCR
        if (i % 40 == 0) {
            pkt[3] = 0x30; // AF + Payload
            pkt[4] = 7;    // AF length
            pkt[5] = 0x10; // PCR flag
            uint64_t t = (*now_ns * 27) / 1000; 
            uint64_t base = t / 300;
            uint16_t ext = t % 300;
            
            pkt[6] = (base >> 25) & 0xFF;
            pkt[7] = (base >> 17) & 0xFF;
            pkt[8] = (base >> 9) & 0xFF;
            pkt[9] = (base >> 1) & 0xFF;
            pkt[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
            pkt[11] = ext & 0xFF;
        } else {
            pkt[3] = 0x10;
        }
        
        tsa_process_packet(h, pkt, *now_ns);
        *now_ns += interval_ns;
    }
}

void test_pid_bitrate_ema() {
    printf("Testing PID Bitrate EMA Smoothing...\n");
    
    tsa_config_t cfg = {
        .is_live = true,
        .pcr_ema_alpha = 0.1 // 10% weight to new value
    };
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint64_t now = 0;
    // Step 1: 1 Mbps (interval ~1.506ms)
    // Send 1328 packets (~2 seconds at 1Mbps)
    send_packets(h, 0x100, 1328, &now, 1506024);

    tsa_commit_snapshot(h, now);
    tsa_snapshot_full_t snap1;
    tsa_take_snapshot_full(h, &snap1);
    
    uint64_t br1 = 0;
    for(int i=0; i<8192; i++) {
        if(snap1.pids[i].pid == 0x100) {
            br1 = (uint64_t)(snap1.pids[i].bitrate_q16_16 / 65536.0);
            break;
        }
    }
    printf("Step 1 (Stable 1Mbps): %lu bps\n", br1);
    assert(br1 > 800000 && br1 < 1200000);

    // Step 2: Sudden jump to 10 Mbps (interval ~150us)
    // Send for another 1 second
    send_packets(h, 0x100, 6640, &now, 150602);

    tsa_commit_snapshot(h, now);
    tsa_snapshot_full_t snap2;
    tsa_take_snapshot_full(h, &snap2);
    
    uint64_t br2 = 0;
    for(int i=0; i<8192; i++) {
        if(snap2.pids[i].pid == 0x100) {
            br2 = (uint64_t)(snap2.pids[i].bitrate_q16_16 / 65536.0);
            break;
        }
    }
    printf("Step 2 (Jump to 10Mbps, EMA alpha=0.1): %lu bps\n", br2);
    
    // With EMA (alpha=0.1): 0.1 * 10M + 0.9 * 1M = 1.9M.
    assert(br2 < 4000000); 
    assert(br2 > 1200000);

    tsa_destroy(h);
    printf("[PASS] EMA smoothing verified.\n");
}

int main() {
    test_pid_bitrate_ema();
    return 0;
}
