#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

extern void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz);

void test_prometheus_labels() {
    printf("Running Prometheus Labels Unit Test...\n");

    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_LIVE;
    strncpy(cfg.input_label, "TEST-LABEL-XYZ", 31);

    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);
    assert(strcmp(h->config.input_label, "TEST-LABEL-XYZ") == 0);

    /* Mock some data to ensure health score and lock status exist */
    uint8_t pkt[188] = {0};
    pkt[0] = 0x47;
    tsa_process_packet(h, pkt, NS_PER_SEC);
    tsa_commit_snapshot(h, NS_PER_SEC + 100000000ULL);

    char* buf = malloc(64 * 1024);
    tsa_exporter_prom_v2(&h, 1, buf, 64 * 1024);

    printf("Exported Metrics Sample:\n%.200s...\n", buf);

    /* Verify label presence */
    assert(strstr(buf, "stream_id=\"TEST-LABEL-XYZ\"") != NULL);
    assert(strstr(buf, "stream_id=\"unknown\"") == NULL);

    printf("Prometheus labels verified successfully!\n");

    free(buf);
    tsa_destroy(h);
}

int main() {
    test_prometheus_labels();
    return 0;
}
