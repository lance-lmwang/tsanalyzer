#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"

void test_v35_schema_integrity() {
    printf("Verifying v3.5 JSON Schema Integrity...\n");
    tsa_snapshot_full_t snap = {0};

    // Set unique markers for every tier
    snap.summary.master_health = 88.8f;
    snap.srt.rtt_ms = 123;
    snap.stats.video_fps = 59.94f;
    snap.predictive.rst_network_s = 9.9f;

    char buf[1024 * 64];
    tsa_snapshot_to_json(&snap, buf, sizeof(buf));

    // Tier 0/1
    assert(strstr(buf, "\"master_health\":88.8") != NULL);
    assert(strstr(buf, "\"srt_rtt_ms\":123") != NULL);

    // Tier 3
    assert(strstr(buf, "\"video_fps\":59.94") != NULL);

    // Tier 4
    assert(strstr(buf, "\"rst_network_s\":9.90") != NULL);

    printf("[PASS] JSON Serializer matches v3.5 C-structure markers.\n");
}

int main() {
    test_v35_schema_integrity();
    return 0;
}
