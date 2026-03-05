#include "tsa_internal.h"
#include <stdio.h>
#include <string.h>

void tsa_render_dashboard(tsa_handle_t* h) {
    if (!h) return;
    tsa_snapshot_full_t snap;
    tsa_take_snapshot_full(h, &snap);
    time_t now_t = time(NULL);
    char* ct = ctime(&now_t);
    if (ct) ct[strlen(ct) - 1] = '\0';
    printf("\n================================================================================\n");
    printf(" TSA PROFESSIONAL METROLOGY REPORT                                %s\n", ct);
    printf("================================================================================\n");
    printf(" Signal Status:  %s\n", snap.summary.signal_lock ? "LOCKED" : "LOSS");
    if (snap.service_name[0]) {
        printf(" Service:        %s\n", snap.service_name);
        printf(" Provider:       %s\n", snap.provider_name);
    }
    printf(" Total Packets:  %llu\n", (unsigned long long)snap.summary.total_packets);
    printf(" PCR Bitrate:    %.2f Mbps\n", (double)snap.stats.pcr_bitrate_bps / 1e6);
    printf(" AV Sync:        %d ms\n", snap.stats.av_sync_ms);
    printf("--------------------------------------------------------------------------------\n");
    printf(" PID      | TYPE       | BITRATE (Mbps) | EB FILL | I/P/B Frames\n");
    printf("--------------------------------------------------------------------------------\n");
    for (uint32_t i = 0; i < snap.active_pid_count; i++) {
        tsa_pid_info_t* p = &snap.pids[i];
        printf(" 0x%04x   | %-10s | %15.3f | %5.1f%% | %llu/%llu/%llu\n", p->pid, p->type_str,
               (double)(p->bitrate_q16_16 >> 16) / 1e6, p->eb_fill_pct, (unsigned long long)p->i_frames,
               (unsigned long long)p->p_frames, (unsigned long long)p->b_frames);
    }
    printf("================================================================================\n");

    // Drain and display events
    bool header_printed = false;
    size_t head = atomic_load_explicit(&h->event_q->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&h->event_q->tail, memory_order_relaxed);

    while (tail < head) {
        if (!header_printed) {
            printf("\n RECENT BROADCAST EVENTS:\n");
            printf("--------------------------------------------------------------------------------\n");
            header_printed = true;
        }
        tsa_event_t* ev = &h->event_q->events[tail % MAX_EVENT_QUEUE];
        const char* type_str = "UNKNOWN";
        switch (ev->type) {
            case TSA_EVENT_CC_ERROR:
                type_str = "CC ERROR";
                break;
            case TSA_EVENT_PCR_JITTER:
                type_str = "PCR JITTER";
                break;
            case TSA_EVENT_PCR_REPETITION:
                type_str = "PCR REPETITION";
                break;
            case TSA_EVENT_SCTE35:
                type_str = "SCTE-35";
                break;
            case TSA_EVENT_SYNC_LOSS:
                type_str = "SYNC LOSS";
                break;
            case TSA_EVENT_CRC_ERROR:
                type_str = "CRC ERROR";
                break;
            case TSA_EVENT_PAT_TIMEOUT:
                type_str = "PAT TIMEOUT";
                break;
            case TSA_EVENT_PMT_TIMEOUT:
                type_str = "PMT TIMEOUT";
                break;
        }
        printf(" [%10llu] %-15s PID 0x%04x  Value: %llu\n", (unsigned long long)ev->timestamp_ns / 1000000ULL, type_str,
               ev->pid, (unsigned long long)ev->value);
        tail++;
    }
    atomic_store_explicit(&h->event_q->tail, tail, memory_order_release);
    if (header_printed) printf("================================================================================\n");
    printf("\n");
}
