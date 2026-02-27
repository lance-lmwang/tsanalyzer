#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_mdi_logic() {
    printf("Running test_mdi_logic...\n");
    tsa_config_t cfg = {0};
    cfg.forced_cbr_bitrate = 10000000;  // 10Mbps
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;

    // Constant bitrate arrival (perfectly paced)
    // 10Mbps = 1.25MB/s = ~6648 packets/s = ~150.4us per packet
    uint64_t now = 1000000000ULL;
    for (int i = 0; i < 1000; i++) {
        tsa_process_packet(h, pkt, now);
        now += 150428ULL;  // ~150.4us
    }

    tsa_commit_snapshot(h, now);
    tsa_snapshot_full_t s;
    tsa_take_snapshot_full(h, &s);

    printf("DF: %.2f ms, MLR: %.2f pkts/s\n", s.stats.mdi_df_ms, s.stats.mdi_mlr_pkts_s);
    assert(s.stats.mdi_mlr_pkts_s == 0.0);

    tsa_destroy(h);
    printf("test_mdi_logic passed.\n");
}

int main() {
    test_mdi_logic();
    return 0;
}
