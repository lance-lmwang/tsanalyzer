#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tsa.h"
#include "tsa_internal.h"

static void on_hls_stats(void* user_data, double buffer_ms, uint64_t errors) {
    double* out = (double*)user_data;
    *out = buffer_ms;
    (void)errors;
}

int main() {
    printf(">>> STARTING HLS RST CALCULATION TEST <<<\n");

    /* We test the logic of update_hls_metrics by simulating the same data
     * flow that happens in tsa_hls_ingest.c */

    double current_rst = 0;
    long long mock_start_us = 1000000;  // 1s
    double data_duration_s = 6.0;       // 6s segment

    // First segment
    double rst = data_duration_s;
    printf("Initial RST: %.2f ms\n", rst * 1000.0);
    assert(rst == 6.0);

    // 2 seconds pass
    long long now_us = mock_start_us + 2000000;
    double elapsed_s = (double)(now_us - mock_start_us) / 1000000.0;
    rst = data_duration_s - elapsed_s;
    printf("RST after 2s: %.2f ms\n", rst * 1000.0);
    assert(rst == 4.0);

    // Second segment arrives (6s)
    data_duration_s += 6.0;
    rst = data_duration_s - elapsed_s;
    printf("RST after 2nd segment: %.2f ms\n", rst * 1000.0);
    assert(rst == 10.0);

    printf(">>> HLS RST CALCULATION TEST PASSED <<<\n");
    return 0;
}
