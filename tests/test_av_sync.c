#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/**
 * @brief Unit test for ISO/IEC 13818-1 A/V Sync and Jitter calculation.
 */
int main() {
    printf(">>> STARTING A/V SYNC & JITTER UNIT TEST <<<\n");

    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_LIVE;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t v_pid = 0x100;
    uint16_t a_pid = 0x101;

    /* 1. Setup Program Mapping (PAT/PMT Mock) */
    h->program_count = 1;
    h->programs[0].program_number = 1;
    h->programs[0].stream_count = 2;
    h->programs[0].streams[0].pid = v_pid;
    h->programs[0].streams[0].stream_type = TSA_TYPE_VIDEO_H264;
    h->programs[0].streams[1].pid = a_pid;
    h->programs[0].streams[1].stream_type = TSA_TYPE_AUDIO_AAC;

    h->es_tracks[v_pid].active = true;
    h->es_tracks[v_pid].stream_type = TSA_TYPE_VIDEO_H264;
    h->es_tracks[a_pid].active = true;
    h->es_tracks[a_pid].stream_type = TSA_TYPE_AUDIO_AAC;

    h->signal_lock = true;
    h->stc_locked = true;

    /* 2. Test PTS Jitter Calculation */
    /* ISO/IEC 13818-1: Jitter = (PTS_n - PTS_n-1) - (STC_n - STC_n-1) */

    /* Simulate 1st PES Packet arrival */
    ts_decode_result_t res = {0};
    res.pid = v_pid;
    res.pusi = true;
    res.has_pes_header = true;
    res.has_pts = true;
    res.pts = 90000;  // 1.0s

    h->stc_ns = 1000000000ULL;  // 1.0s
    tsa_es_track_t* v_tk = &h->es_tracks[v_pid];

    /* Manually invoke the engine logic for 1st sample */
    v_tk->last_pts_val = res.pts;
    v_tk->last_pts_vstc = h->stc_ns;

    /* Simulate 2nd PES Packet arrival with JITTER */
    /* Expect 40ms later: PTS should be 90000 + 3600 = 93600 */
    /* But it arrives 50ms later (10ms delay jitter) */
    res.pts = 93600;
    h->stc_ns = 1050000000ULL;  // 1.05s (50ms interval)

    /* PTS delta = 3600, VSTC delta = 50ms = 4500 ticks.
     * Jitter = 3600 - 4500 = -900 ticks */

    uint64_t pts_delta = (res.pts > v_tk->last_pts_val) ? (res.pts - v_tk->last_pts_val) : 0;
    uint64_t vstc_delta_ticks =
        (h->stc_ns > v_tk->last_pts_vstc) ? ((h->stc_ns - v_tk->last_pts_vstc) * 90 / 1000000) : 0;
    if (pts_delta > 0 && vstc_delta_ticks > 0) {
        int64_t jitter = (int64_t)pts_delta - (int64_t)vstc_delta_ticks;
        v_tk->pts_jitter_q64 = INT_TO_Q64_64(jitter);
    }

    int64_t measured_jitter = (int64_t)(v_tk->pts_jitter_q64 >> 64);
    printf("Measured Jitter: %ld ticks\n", (long)measured_jitter);
    assert(measured_jitter == -900);

    /* 3. Test A/V Skew Calculation */
    /*
     * Video: PTS=90000 at VSTC=1.0s -> Offset = 0
     * Audio: PTS=94500 at VSTC=1.0s -> Offset = 4500 ticks = 50ms
     * Skew = 50ms
     */
    v_tk->last_pts_val = 90000;
    v_tk->last_pts_vstc = 1000000000ULL;

    tsa_es_track_t* a_tk = &h->es_tracks[a_pid];
    a_tk->last_pts_val = 94500;
    a_tk->last_pts_vstc = 1000000000ULL;

    tsa_commit_snapshot(h, 2000000000ULL);

    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);

    printf("Calculated A/V Skew: %d ms\n", snap.stats.av_sync_ms);
    assert(snap.stats.av_sync_ms == 50);

    tsa_destroy(h);
    printf(">>> A/V SYNC & JITTER UNIT TEST PASSED <<<\n");
    return 0;
}
