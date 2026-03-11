#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

int main() {
    printf(">>> STARTING T-STD OPERATORS UNIT TEST <<<\n");

    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_LIVE;  // Use LIVE to simplify timing for this mock
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t pid = 0x100;
    tsa_es_track_t* es = &h->es_tracks[pid];
    es->stream_type = TSA_TYPE_VIDEO_H264;
    h->pid_seen[pid] = true;

    /* Pre-set bitrate to ensure leak rates are calculated */
    h->live->pid_bitrate_bps[pid] = 10000000;

    /* 1. Test TB Filling */
    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = (pid >> 8) & 0x1F;
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x10;  // Payload only

    /* Force signal lock */
    h->signal_lock = true;

    uint64_t now = 1000000000ULL;
    tsa_feed_data(h, pkt, 188, now);

    uint64_t tb_fill = (uint64_t)(es->tstd.tb_fill_q64 >> 64);
    printf("TB Fill: %lu bits\n", tb_fill);
    assert(tb_fill == 1504);

    /* 2. Test TB -> MB/EB Leakage */
    /* Advance wall clock time significantly to ensure leak */
    now += 500000000ULL;  // 500ms
    tsa_feed_data(h, pkt, 188, now);

    /* The old 1504 should have leaked, plus the new 1504 added.
     * But since 500ms at 10Mbps is 5Mbits, TB must be emptied before new pkt. */
    uint64_t tb_fill_after = (uint64_t)(es->tstd.tb_fill_q64 >> 64);
    uint64_t mb_fill = (uint64_t)(es->tstd.mb_fill_q64 >> 64);
    uint64_t eb_fill = (uint64_t)(es->tstd.eb_fill_q64 >> 64);
    printf("After 500ms: TB=%lu, MB=%lu, EB=%lu\n", tb_fill_after, mb_fill, eb_fill);

    /* In this implementation, TB fills THEN leaks in essence_on_ts.
     * So it should have 1504 left from the packet just fed. */
    assert(tb_fill_after == 1504);
    assert(mb_fill + eb_fill >= 1504);

    /* 3. Test AU Queuing and Draining */
    es->pes.pending_dts_ns = h->stc_ns + 200000000ULL;
    es->pes.total_length = 100;
    es->pes.ref_count = 1;

    pkt[1] |= 0x40;  // PUSI=1
    tsa_feed_data(h, pkt, 188, now);

    assert(es->au_q.head != es->au_q.tail);
    uint64_t eb_before_drain = (uint64_t)(es->tstd.eb_fill_q64 >> 64);

    /* Advance STC to DTS and drain */
    h->stc_ns = es->au_q.queue[es->au_q.head].dts_ns + 1;
    tsa_tstd_drain(h, pid);

    assert(es->au_q.head == es->au_q.tail);
    uint64_t eb_after_drain = (uint64_t)(es->tstd.eb_fill_q64 >> 64);
    printf("EB Drain: Before=%lu, After=%lu\n", eb_before_drain, eb_after_drain);
    assert(eb_after_drain < eb_before_drain);

    /* 4. Test Underflow Detection */
    h->event_q->tail = h->event_q->head;
    es->pes.pending_dts_ns = h->stc_ns - 1000000ULL;
    es->pes.total_length = 100;
    es->pes.ref_count = 1;
    tsa_feed_data(h, pkt, 188, now);

    bool found_underflow = false;
    uint32_t head = atomic_load(&h->event_q->head);
    uint32_t tail = atomic_load(&h->event_q->tail);
    while (head != tail) {
        if (h->event_q->events[head].type == TSA_EVENT_TSTD_UNDERFLOW) {
            found_underflow = true;
            break;
        }
        head = (head + 1) % MAX_EVENT_QUEUE;
    }
    assert(found_underflow);
    printf("Underflow event correctly triggered.\n");

    /* 5. Test Predictive Underflow Detection */
    h->event_q->tail = h->event_q->head;               // Clear queue
    es->pes.pending_dts_ns = h->stc_ns + 10000000ULL;  // 10ms in the future
    es->pes.total_length = 500000;                     // Need 500KB immediately
    es->pes.ref_count = 1;
    tsa_feed_data(h, pkt, 188, now);

    bool found_predictive = false;
    head = atomic_load(&h->event_q->head);
    tail = atomic_load(&h->event_q->tail);
    while (head != tail) {
        if (h->event_q->events[head].type == TSA_EVENT_TSTD_PREDICTIVE) {
            found_predictive = true;
            break;
        }
        head = (head + 1) % MAX_EVENT_QUEUE;
    }
    assert(found_predictive);
    printf("Predictive Underflow event correctly triggered.\n");

    tsa_destroy(h);
    printf(">>> T-STD OPERATORS UNIT TEST PASSED <<<\n");
    return 0;
}
