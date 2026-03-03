#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz) {
    if (!handles || !buf || sz < 4096) return;
    int off = 0;
    int n;

#define SAFE_APPEND(...)                                                           \
    {                                                                              \
        n = snprintf(buf + off, (sz > (size_t)off) ? (sz - off) : 0, __VA_ARGS__); \
        if (n > 0) off += (off + n < (int)sz) ? n : (int)(sz - off - 1);           \
    }

    // Global Correlation Analysis
    int alarmed_streams = 0;
    int valid_streams = 0;
    for (int i = 0; i < count; i++) {
        if (handles[i]) {
            tsa_snapshot_lite_t s;
            if (tsa_take_snapshot_lite(handles[i], &s) == 0) {
                if (s.total_packets > 0) {
                    valid_streams++;
                    if (s.master_health < 80.0) alarmed_streams++;
                }
            }
        }
    }
    if (valid_streams >= 3 && (double)alarmed_streams / valid_streams > 0.5) {
        SAFE_APPEND("# HELP tsa_global_network_incident 1 if more than 50%% streams are failing\n");
        SAFE_APPEND("tsa_global_network_incident 1\n");
    } else {
        SAFE_APPEND("tsa_global_network_incident 0\n");
    }

    static __thread tsa_snapshot_full_t snap_storage;
    tsa_snapshot_full_t* snap = &snap_storage;

    for (int i = 0; i < count; i++) {
        tsa_handle_t* h = handles[i];
        if (!h) continue;

        if (tsa_take_snapshot_full(h, snap) != 0) continue;

        const tsa_tr101290_stats_t* s = &snap->stats;
        const char* sid = h->config.input_label;
        if (!sid[0]) sid = "unknown";

        char labels[128];
        snprintf(labels, sizeof(labels), "{stream_id=\"%s\"}", sid);

        // Tier 1: Master Control Console (SIGNAL STATUS)
        SAFE_APPEND("tsa_signal_lock_status%s %d\n", labels, snap->summary.signal_lock ? 1 : 0);
        SAFE_APPEND("tsa_health_score%s %.1f\n", labels, snap->predictive.master_health);
        SAFE_APPEND("tsa_internal_analyzer_drop%s %llu\n", labels, (unsigned long long)s->internal_analyzer_drop);
        SAFE_APPEND("tsa_worker_slice_overruns%s %llu\n", labels, (unsigned long long)s->worker_slice_overruns);

        // Tier 2: Transport & Link Integrity (SRT/MDI)
        SAFE_APPEND("tsa_srt_rtt_ms%s %lld\n", labels, (long long)snap->srt.rtt_ms);
        SAFE_APPEND("mdi_mlr%s %.2f\n", labels, (float)s->mdi_mlr_pkts_s);
        SAFE_APPEND("mdi_df%s %.2f\n", labels, (float)s->mdi_df_ms);
        SAFE_APPEND("srt_retransmit_rate%s %.4f\n", labels, (float)snap->srt.retransmit_tax);
        SAFE_APPEND("tsa_mdi_delay_factor_ms%s %.2f\n", labels, (float)s->mdi_df_ms);

        // Tier 3: ETR 290 P1 (CRITICAL COMPLIANCE)
        SAFE_APPEND("tsa_tr101290_p1_sync_loss%s %llu\n", labels, (unsigned long long)s->sync_loss.count);
        SAFE_APPEND("tsa_tr101290_p1_pat_error%s %llu\n", labels, (unsigned long long)s->pat_error.count);
        SAFE_APPEND("tsa_tr101290_p1_pmt_error%s %llu\n", labels, (unsigned long long)s->pmt_error.count);
        SAFE_APPEND("tsa_tr101290_p1_cc_error%s %llu\n", labels, (unsigned long long)s->cc_error.count);
        SAFE_APPEND("tsa_tr101290_p1_pid_error%s %llu\n", labels, (unsigned long long)s->pid_error.count);
        SAFE_APPEND("tsa_tr101290_p2_pts_error%s %llu\n", labels, (unsigned long long)s->pts_error.count);
        SAFE_APPEND("tsa_tr101290_p2_crc_error%s %llu\n", labels, (unsigned long long)s->crc_error.count);
        SAFE_APPEND("tsa_tr101290_p2_transport_error%s %llu\n", labels, (unsigned long long)s->transport_error.count);

        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"sync_loss\"} %llu\n", sid,
                    (unsigned long long)s->sync_loss.count);
        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"pat_error\"} %llu\n", sid,
                    (unsigned long long)s->pat_error.count);
        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"cc_error\"} %llu\n", sid,
                    (unsigned long long)s->cc_error.count);
        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"pmt_error\"} %llu\n", sid,
                    (unsigned long long)s->pmt_error.count);
        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"pid_error\"} %llu\n", sid,
                    (unsigned long long)s->pid_error.count);
        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"pts_error\"} %llu\n", sid,
                    (unsigned long long)s->pts_error.count);
        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"crc_error\"} %llu\n", sid,
                    (unsigned long long)s->crc_error.count);
        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"transport_error\"} %llu\n", sid,
                    (unsigned long long)s->transport_error.count);

        SAFE_APPEND("tsa_transport_error_count%s %llu\n", labels, (unsigned long long)s->transport_error.count);
        
        // Tier 4: ETR 290 P2 (CLOCK & TIMING)
        SAFE_APPEND("tsa_pcr_jitter_ms%s %.3f\n", labels, s->pcr_jitter_avg_ns / 1000000.0);
        SAFE_APPEND("tsa_pcr_repetition_errors%s %llu\n", labels, (unsigned long long)s->pcr_repetition_error.count);
        SAFE_APPEND("tsa_pcr_accuracy_errors%s %llu\n", labels, (unsigned long long)s->pcr_accuracy_error.count);
        SAFE_APPEND("tsa_pcr_accuracy_ns%s %.2f\n", labels, (float)s->pcr_accuracy_ns);

        // Tier 5: Service Payload Dynamics (MUX)
        SAFE_APPEND("tsa_physical_bitrate_bps%s %llu\n", labels, (unsigned long long)s->physical_bitrate_bps);
        SAFE_APPEND("tsa_pcr_bitrate_bps%s %llu\n", labels, (unsigned long long)s->pcr_bitrate_bps);

        // Tier 6: Essence Quality & Temporal Stability
        SAFE_APPEND("tsa_essence_video_fps%s %.2f\n", labels, (float)s->video_fps);
        SAFE_APPEND("tsa_gop_ms%s %u\n", labels, s->gop_ms);
        SAFE_APPEND("tsa_essence_av_sync_ms%s %d\n", labels, s->av_sync_ms);

        // Tier 7: Alarm Recap (Inference factors)
        SAFE_APPEND("tsa_rst_network_seconds%s %.2f\n", labels, snap->predictive.rst_network_s);
        SAFE_APPEND("tsa_engine_processing_latency_ns%s %llu\n", labels,
                    (unsigned long long)s->engine_processing_latency_ns);

        // PID Details & T-STD Buffer Metrics
        for (uint32_t j = 0; j < snap->active_pid_count; j++) {
            uint16_t p = snap->pids[j].pid;
            const char* t = snap->pids[j].type_str[0] ? snap->pids[j].type_str : "Unknown";
            char pid_labels[256];
            snprintf(pid_labels, sizeof(pid_labels), "{stream_id=\"%s\",pid=\"0x%04x\",type=\"%s\"}", sid, p, t);

            SAFE_APPEND("tsa_pid_bitrate_bps%s %llu\n", pid_labels, (unsigned long long)s->pid_bitrate_bps[p]);
            
            // T-STD Metrics
            if (snap->pids[j].eb_fill_pct > 0 || snap->pids[j].tb_fill_pct > 0) {
                SAFE_APPEND("tsa_pid_tstd_eb_fill_pct%s %.2f\n", pid_labels, snap->pids[j].eb_fill_pct);
                SAFE_APPEND("tsa_pid_tstd_tb_fill_pct%s %.2f\n", pid_labels, snap->pids[j].tb_fill_pct);
                SAFE_APPEND("tsa_pid_tstd_mb_fill_pct%s %.2f\n", pid_labels, snap->pids[j].mb_fill_pct);
            }

            if (snap->pids[j].width > 0) {
                char v_labels[256];
                snprintf(v_labels, sizeof(v_labels), "{stream_id=\"%s\",pid=\"0x%04x\"}", sid, p);
                SAFE_APPEND("tsa_video_width%s %u\n", v_labels, snap->pids[j].width);
                SAFE_APPEND("tsa_video_height%s %u\n", v_labels, snap->pids[j].height);
                if (snap->pids[j].gop_n > 0) {
                    SAFE_APPEND("tsa_video_gop_n%s %u\n", v_labels, snap->pids[j].gop_n);
                }
            }
        }

    }
#undef SAFE_APPEND
}

// Compatibility wrapper for older tests
void tsa_export_prometheus(tsa_handle_t* h, char* buf, size_t sz) {
    tsa_exporter_prom_v2(&h, 1, buf, sz);
}

void tsa_export_pid_labels(tsa_metric_buffer_t* buf, tsa_handle_t* h, uint16_t pid) {
    if (!buf || !h || pid >= TS_PID_MAX) return;

    char label[128];
    const char* codec = tsa_get_pid_type_name(h, pid);
    const char* type = "Other";

    if (strcmp(codec, "H.264") == 0 || strcmp(codec, "HEVC") == 0 || strcmp(codec, "MPEG2-V") == 0) {
        type = "Video";
    } else if (strcmp(codec, "AAC") == 0 || strcmp(codec, "ADTS-AAC") == 0 || strcmp(codec, "MPEG1-A") == 0 ||
               strcmp(codec, "MPEG2-A") == 0 || strcmp(codec, "AC3") == 0) {
        type = "Audio";
    }

    snprintf(label, sizeof(label), "pid=\"0x%04x\",type=\"%s\",codec=\"%s\"", pid, type, codec);
    tsa_mbuf_append_str(buf, label);
}
