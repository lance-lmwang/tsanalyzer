#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tsa.h"
#include "tsa_internal.h"

// THE LIGHTEST ENGINE EVER MADE
static tsa_handle_t g_h;

int128_t ts_now_ns128(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int128_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

tsa_handle_t* tsa_create(const tsa_config_t* cfg) {
    memset(&g_h, 0, sizeof(g_h));
    if(cfg) g_h.config = *cfg;
    g_h.start_ns = ts_now_ns128();
    g_h.last_snap_ns = g_h.start_ns;
    return &g_h;
}

void tsa_destroy(tsa_handle_t* h) { (void)h; }

int tsa_process_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t now) {
    if (!h || !pkt || pkt[0] != 0x47) return -1;
    h->live.total_ts_packets++;
    return 0;
}

void tsa_commit_snapshot(tsa_handle_t* h, uint64_t now) {
    if (!h) return;
    uint64_t ds_ns = (now > h->last_snap_ns) ? (now - h->last_snap_ns) : 1000000;
    if (ds_ns < 10000000ULL) return; // 10ms min

    uint64_t dp = h->live.total_ts_packets - h->prev_snap_base.total_ts_packets;
    double ds_sec = (double)ds_ns / 1e9;
    
    uint64_t cur_br = (uint64_t)((dp * 188.0 * 8.0) / ds_sec);
    h->live.physical_bitrate_bps = (uint64_t)(cur_br * 0.2 + h->live.physical_bitrate_bps * 0.8);
    
    // ATOMIC-ISH UPDATE WITHOUT STRUCT COPY
    h->snap_state.stats.summary.master_health = 100.0f;
    h->snap_state.stats.summary.total_packets = h->live.total_ts_packets;
    h->snap_state.stats.summary.physical_bitrate_bps = h->live.physical_bitrate_bps;
    h->snap_state.stats.stats.total_ts_packets = h->live.total_ts_packets;
    h->snap_state.stats.stats.physical_bitrate_bps = h->live.physical_bitrate_bps;

    h->prev_snap_base.total_ts_packets = h->live.total_ts_packets;
    h->last_snap_ns = now;
}

int tsa_take_snapshot_full(tsa_handle_t* h, tsa_snapshot_full_t* s) {
    if (!h || !s) return -1;
    s->summary = h->snap_state.stats.summary;
    s->stats = h->snap_state.stats.stats;
    return 0;
}

size_t tsa_snapshot_to_json(const tsa_snapshot_full_t* snap, char* buf, size_t sz) {
    return snprintf(buf, sz, "{\"status\":{\"master_health\":%.1f},\"tier1_link\":{\"physical_bitrate_bps\":%llu}}",
        snap->summary.master_health, (unsigned long long)snap->summary.physical_bitrate_bps);
}

void tsa_export_prometheus(tsa_handle_t* h, char* buffer, size_t size) {
    snprintf(buffer, size, "tsa_total_packets %llu\ntsa_physical_bitrate_bps %llu\n",
        (unsigned long long)h->live.total_ts_packets, (unsigned long long)h->live.physical_bitrate_bps);
}
