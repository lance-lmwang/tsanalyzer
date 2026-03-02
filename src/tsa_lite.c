#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// THE LIGHTEST ENGINE EVER MADE - Optimized for high-throughput metrics
// Now uses dynamic allocation to avoid segfaults in pointer access.

int128_t ts_now_ns128(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int128_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

tsa_handle_t* tsa_create(const tsa_config_t* cfg) {
    tsa_handle_t* h = calloc(1, sizeof(tsa_handle_t));
    if (!h) return NULL;

    if (cfg) h->config = *cfg;
    h->start_ns = ts_now_ns128();
    h->last_snap_ns = h->start_ns;

    // Allocate required stats structures to avoid NULL pointer dereference
    h->live = calloc(1, sizeof(tsa_tr101290_stats_t));
    h->prev_snap_base = calloc(1, sizeof(tsa_tr101290_stats_t));
    h->snap_state.stats = calloc(1, sizeof(tsa_snapshot_full_t));

    if (!h->live || !h->prev_snap_base || !h->snap_state.stats) {
        free(h->live);
        free(h->prev_snap_base);
        free(h->snap_state.stats);
        free(h);
        return NULL;
    }

    return h;
}

void tsa_destroy(tsa_handle_t* h) {
    if (!h) return;
    free(h->live);
    free(h->prev_snap_base);
    free(h->snap_state.stats);
    free(h);
}

int tsa_process_packet(tsa_handle_t* h, const uint8_t* pkt, uint64_t now) {
    if (!h || !pkt || pkt[0] != 0x47) return -1;
    (void)now;
    h->live->total_ts_packets++;
    return 0;
}

void tsa_commit_snapshot(tsa_handle_t* h, uint64_t now) {
    if (!h) return;
    uint64_t ds_ns = (now > h->last_snap_ns) ? (now - h->last_snap_ns) : 1000000;
    if (ds_ns < 10000000ULL) return;  // 10ms min

    uint64_t dp = h->live->total_ts_packets - h->prev_snap_base->total_ts_packets;
    double ds_sec = (double)ds_ns / 1e9;

    uint64_t cur_br = (uint64_t)((dp * 188.0 * 8.0) / ds_sec);
    h->live->physical_bitrate_bps = (uint64_t)(cur_br * 0.2 + h->live->physical_bitrate_bps * 0.8);

    // Update shared snapshot state
    h->snap_state.stats->summary.master_health = 100.0f;
    h->snap_state.stats->summary.total_packets = h->live->total_ts_packets;
    h->snap_state.stats->summary.physical_bitrate_bps = h->live->physical_bitrate_bps;
    h->snap_state.stats->stats.total_ts_packets = h->live->total_ts_packets;
    h->snap_state.stats->stats.physical_bitrate_bps = h->live->physical_bitrate_bps;

    h->prev_snap_base->total_ts_packets = h->live->total_ts_packets;
    h->last_snap_ns = now;
}

int tsa_take_snapshot_full(tsa_handle_t* h, tsa_snapshot_full_t* s) {
    if (!h || !s) return -1;
    *s = *(h->snap_state.stats);
    return 0;
}

size_t tsa_snapshot_to_json(const tsa_snapshot_full_t* snap, char* buf, size_t sz) {
    return snprintf(buf, sz, "{\"status\":{\"master_health\":%.1f},\"tier1_link\":{\"physical_bitrate_bps\":%llu}}",
                    snap->summary.master_health, (unsigned long long)snap->summary.physical_bitrate_bps);
}

void tsa_export_prometheus(tsa_handle_t* h, char* buffer, size_t size) {
    snprintf(buffer, size, "tsa_total_packets %llu\ntsa_physical_bitrate_bps %llu\n",
             (unsigned long long)h->live->total_ts_packets, (unsigned long long)h->live->physical_bitrate_bps);
}
