#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_full_prometheus_export() {
    printf("Running test_full_prometheus_export...\n");
    tsa_config_t cfg = {.input_label = "test_run"};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    // Simulate some activity to populate metrics
    uint8_t pkt[188] = {TS_SYNC_BYTE, 0x01, 0x00, 0x10};
    tsa_process_packet(h, pkt, 1000000000ULL);
    tsa_process_packet(h, pkt, 1100000000ULL);
    tsa_commit_snapshot(h, 2000000000ULL);

    char buffer[16 * 1024];  // 16KB buffer
    tsa_export_prometheus(h, buffer, sizeof(buffer));

    // Verify it generated some data
    assert(strlen(buffer) > 0);

    // Check for some expected core metrics
    assert(strstr(buffer, "# HELP tsa_pcr_bitrate_bps") != NULL);
    assert(strstr(buffer, "# TYPE tsa_pcr_bitrate_bps gauge") != NULL);
    assert(strstr(buffer, "tsa_pcr_bitrate_bps{stream_id=\"test_run\"}") != NULL);

    // Check for P1 errors
    assert(strstr(buffer, "tsa_tr101290_p1_errors{stream_id=\"test_run\", error_type=\"cc\"}") != NULL);

    assert(strstr(buffer, "# HELP tsa_mdi_delay_factor_ms") != NULL);
    assert(strstr(buffer, "# TYPE tsa_mdi_delay_factor_ms gauge") != NULL);
    assert(strstr(buffer, "tsa_mdi_delay_factor_ms{stream_id=\"test_run\"}") != NULL);

    printf("Prometheus Output Length: %zu\n", strlen(buffer));
    // printf("--- Prometheus Output ---\n%s\n--------------------------\n", buffer);

    tsa_destroy(h);
    printf("test_full_prometheus_export passed.\n");
}

int main() {
    // Rename the test to reflect its new purpose
    test_full_prometheus_export();
    return 0;
}
