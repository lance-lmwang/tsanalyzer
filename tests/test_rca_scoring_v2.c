#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void encode_pcr(uint8_t* pkt, uint64_t ticks) {
    uint64_t base = ticks / 300;
    uint16_t ext = ticks % 300;
    pkt[6] = (base >> 25) & 0xFF;
    pkt[7] = (base >> 17) & 0xFF;
    pkt[8] = (base >> 9) & 0xFF;
    pkt[9] = (base >> 1) & 0xFF;
    pkt[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    pkt[11] = ext & 0xFF;
}

void test_rca_ok() {
    printf("Running test_rca_ok...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    
    // Stable stream
    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x10, 0x00};
    for(int i=0; i<10; i++) {
        pkt[3] = 0x10 | (i % 16);
        tsa_process_packet(h, pkt, i * 1000000);
    }

    tsa_commit_snapshot(h, 1000000000);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("Fault Domain: %u\n", snap.predictive.fault_domain);
    assert(snap.predictive.fault_domain == 0);

    tsa_destroy(h);
    printf("test_rca_ok passed.\n");
}

void test_rca_network_fault() {
    printf("Running test_rca_network_fault...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    // Simulating packet loss (CC errors)
    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x10}; 
    tsa_process_packet(h, pkt, 1000000);
    pkt[3] = 0x12; // Loss
    tsa_process_packet(h, pkt, 2000000);

    tsa_commit_snapshot(h, 1000000000);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("MLR: %.2f, Fault Domain: %u\n", snap.stats.mdi_mlr_pkts_s, snap.predictive.fault_domain);
    assert(snap.predictive.fault_domain == 1);

    tsa_destroy(h);
    printf("test_rca_network_fault passed.\n");
}

void test_rca_encoder_fault() {
    printf("Running test_rca_encoder_fault...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    uint8_t pkt_pcr[188] = {0x47, 0x01, 0x00, 0x30, 0x07, 0x10}; 
    encode_pcr(pkt_pcr, 0);
    tsa_process_packet(h, pkt_pcr, 0);
    encode_pcr(pkt_pcr, 40 * 27000);
    tsa_process_packet(h, pkt_pcr, 40000000);
    encode_pcr(pkt_pcr, 80 * 27000); // 30ms jitter
    tsa_process_packet(h, pkt_pcr, 110000000);

    tsa_commit_snapshot(h, 200000000);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("PCR Jitter Max: %ld ns, Fault Domain: %u\n", snap.stats.pcr_jitter_max_ns, snap.predictive.fault_domain);
    assert(snap.predictive.fault_domain == 2);

    tsa_destroy(h);
    printf("test_rca_encoder_fault passed.\n");
}

void test_rca_multi_causal() {
    printf("Running test_rca_multi_causal...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    // 1. Network Issue: MLR
    uint8_t pkt[188] = {0x47, 0x01, 0x00, 0x10}; 
    tsa_process_packet(h, pkt, 1000000);
    pkt[3] = 0x12; // Loss
    tsa_process_packet(h, pkt, 2000000);

    // 2. Encoder Issue: High Jitter
    uint8_t pkt_pcr[188] = {0x47, 0x01, 0x00, 0x30, 0x07, 0x10}; 
    encode_pcr(pkt_pcr, 0);
    tsa_process_packet(h, pkt_pcr, 0);
    encode_pcr(pkt_pcr, 40 * 27000);
    tsa_process_packet(h, pkt_pcr, 40000000);
    encode_pcr(pkt_pcr, 80 * 27000); // 30ms jitter
    tsa_process_packet(h, pkt_pcr, 110000000);

    tsa_commit_snapshot(h, 1000000000);
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("MLR: %.2f, PCR Jitter Max: %ld ns, Fault Domain: %u\n", 
           snap.stats.mdi_mlr_pkts_s, snap.stats.pcr_jitter_max_ns, snap.predictive.fault_domain);
    // score_net = 0.8 (MLR), score_enc = 0.8 (Jitter)
    // Both > 0.4 -> Domain 3
    assert(snap.predictive.fault_domain == 3);

    tsa_destroy(h);
    printf("test_rca_multi_causal passed.\n");
}

int main() {
    test_rca_ok();
    test_rca_network_fault();
    test_rca_encoder_fault();
    test_rca_multi_causal();
    return 0;
}
