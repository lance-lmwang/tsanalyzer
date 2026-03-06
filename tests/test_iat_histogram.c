#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tsa.h"
#include "tsa_internal.h"

int main() {
    printf("Testing IAT Histogram...\n");

    tsa_config_t cfg = {
        .op_mode = TSA_MODE_LIVE,
        .input_label = "test_iat"
    };

    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Initial packet
    uint8_t dummy_pkt[188] = {0x47, 0x00, 0x00, 0x10};
    tsa_process_packet(h, dummy_pkt, 1000000ULL); // 1ms

    // Under 1ms bucket
    tsa_process_packet(h, dummy_pkt, 1500000ULL); // 500us delta -> under_1ms

    // 1-2ms bucket
    tsa_process_packet(h, dummy_pkt, 3000000ULL); // 1.5ms delta -> bucket_1_2ms

    // 2-5ms bucket
    tsa_process_packet(h, dummy_pkt, 6000000ULL); // 3ms delta -> bucket_2_5ms

    // 5-10ms bucket
    tsa_process_packet(h, dummy_pkt, 13000000ULL); // 7ms delta -> bucket_5_10ms

    // 10-100ms bucket
    tsa_process_packet(h, dummy_pkt, 33000000ULL); // 20ms delta -> bucket_10_100ms

    // Over 100ms bucket
    tsa_process_packet(h, dummy_pkt, 183000000ULL); // 150ms delta -> bucket_over_100ms

    // Verify buckets
    assert(h->live->iat_hist.bucket_under_1ms == 1);
    assert(h->live->iat_hist.bucket_1_2ms == 1);
    assert(h->live->iat_hist.bucket_2_5ms == 1);
    assert(h->live->iat_hist.bucket_5_10ms == 1);
    assert(h->live->iat_hist.bucket_10_100ms == 1);
    assert(h->live->iat_hist.bucket_over_100ms == 1);

    tsa_destroy(h);

    printf("IAT Histogram Tests Passed!\n");
    return 0;
}
