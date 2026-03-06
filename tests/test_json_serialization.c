#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"

int main() {
    tsa_snapshot_full_t snap = {0};
    snap.stats.stream_utc_ms = 1708912345000ULL;
    snap.summary.signal_lock = true;
    snap.summary.master_health = 92.5f;
    snap.srt.rtt_ms = 32;
    snap.stats.video_fps = 25.0f;

    // Add one PID
    snap.active_pid_count = 1;
    snap.pids[0].pid = 0x100;
    snap.pids[0].liveness_status = 1;
    snap.pids[0].bitrate_q16_16 = (int64_t)7450000 << 16;

    tsa_config_t cfg = {0};
    tsa_handle_t *h = tsa_create(&cfg);

    char buffer[4096];
    size_t len = tsa_snapshot_to_json(h, &snap, buffer, sizeof(buffer));

    printf("JSON Length: %zu\n", len);
    printf("JSON Output: %s\n", buffer);

    assert(len > 0);
    assert(strstr(buffer, "\"master_health\":92.5") != NULL);
    assert(strstr(buffer, "\"srt_rtt_ms\":32") != NULL);
    assert(strstr(buffer, "\"pid\":\"0x0100\"") != NULL);
    assert(strstr(buffer, "\"bps\":7450000") != NULL);

    tsa_destroy(h);
    printf("JSON serialization verified.\n");
    return 0;
}
