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

    tsa_snapshot_full_t* snap = malloc(sizeof(tsa_snapshot_full_t));
    if (!snap) return;

    for (int i = 0; i < count; i++) {
        tsa_handle_t* h = handles[i];
        if (!h) continue;

        if (tsa_take_snapshot_full(h, snap) != 0) continue;

        const tsa_tr101290_stats_t* s = &snap->stats;
        const char* sid = h->config.input_label;
        if (!sid[0]) sid = "unknown";

        char labels[128];
        snprintf(labels, sizeof(labels), "{stream_id=\"%s\"}", sid);

        // Tier 1: Core
        SAFE_APPEND("tsa_signal_lock_status%s %d\n", labels, snap->summary.signal_lock ? 1 : 0);
        SAFE_APPEND("tsa_health_score%s %.1f\n", labels, snap->predictive.master_health);

        // Tier 2: ETR 290 P1
        SAFE_APPEND("tsa_tr101290_p1_sync_loss%s %llu\n", labels, (unsigned long long)s->sync_loss.count);
        SAFE_APPEND("tsa_tr101290_p1_pat_error%s %llu\n", labels, (unsigned long long)s->pat_error.count);
        SAFE_APPEND("tsa_tr101290_p1_cc_error%s %llu\n", labels, (unsigned long long)s->cc_error.count);

        SAFE_APPEND("tsa_tr101290_p1_errors{stream_id=\"%s\",error_type=\"sync_loss\"} %llu\n", sid,
                    (unsigned long long)s->sync_loss.count);
        SAFE_APPEND("tsa_tr101290_p1_errors{stream_id=\"%s\",error_type=\"pat_error\"} %llu\n", sid,
                    (unsigned long long)s->pat_error.count);
        SAFE_APPEND("tsa_tr101290_p1_errors{stream_id=\"%s\",error_type=\"cc_error\"} %llu\n", sid,
                    (unsigned long long)s->cc_error.count);

        // Compatibility names
        SAFE_APPEND("tsa_continuity_errors_total%s %llu\n", labels, (unsigned long long)s->cc_error.count);
        SAFE_APPEND("tsa_sync_byte_errors_total%s %llu\n", labels, (unsigned long long)s->sync_byte_error.count);
        SAFE_APPEND("tsa_pcr_jitter_ms%s %.3f\n", labels, s->pcr_jitter_avg_ns / 1000000.0);

        // Document Aligned & Inference Engine (L1 Factors)
        SAFE_APPEND("tsa_sync_loss_errors%s %llu\n", labels, (unsigned long long)s->sync_loss.count);
        SAFE_APPEND("tsa_pat_error_count%s %llu\n", labels, (unsigned long long)s->pat_error.count);
        SAFE_APPEND("tsa_pmt_error_count%s %llu\n", labels, (unsigned long long)s->pmt_error.count);
        SAFE_APPEND("tsa_srt_rtt_ms%s %lld\n", labels, (long long)snap->srt.rtt_ms);

        // Canonical Inference Metrics (Mapping to rules.yml)
        SAFE_APPEND("mdi_mlr%s %.2f\n", labels, (float)s->mdi_mlr_pkts_s);
        SAFE_APPEND("mdi_df%s %.2f\n", labels, (float)s->mdi_df_ms);
        SAFE_APPEND("srt_retransmit_rate%s %.4f\n", labels, (float)snap->srt.retransmit_tax);
        SAFE_APPEND("tsa_tstd_overflow_count%s %d\n", labels, 0); // Placeholder for future T-STD engine

        // Tier 3: Analytics
        SAFE_APPEND("tsa_physical_bitrate_bps%s %llu\n", labels, (unsigned long long)s->physical_bitrate_bps);
        SAFE_APPEND("tsa_pcr_bitrate_bps%s %llu\n", labels, (unsigned long long)s->pcr_bitrate_bps);
        SAFE_APPEND("tsa_internal_analyzer_drop%s %llu\n", labels, (unsigned long long)s->internal_analyzer_drop);
        SAFE_APPEND("tsa_worker_slice_overruns%s %llu\n", labels, (unsigned long long)s->worker_slice_overruns);
        SAFE_APPEND("tsa_mdi_delay_factor_ms%s %.2f\n", labels, (float)s->mdi_df_ms);
        SAFE_APPEND("tsa_essence_video_fps%s %.2f\n", labels, (float)s->video_fps);
        SAFE_APPEND("tsa_essence_av_sync_ms%s %d\n", labels, s->av_sync_ms);
        SAFE_APPEND("tsa_engine_processing_latency_ns%s %llu\n", labels,
                    (unsigned long long)s->engine_processing_latency_ns);

        // Tier 4: RST
        SAFE_APPEND("tsa_rst_network_seconds%s %.2f\n", labels, snap->predictive.rst_network_s);

        // PID Details
        for (uint32_t j = 0; j < snap->active_pid_count; j++) {
            uint16_t p = snap->pids[j].pid;
            const char* t = snap->pids[j].type_str[0] ? snap->pids[j].type_str : "Unknown";
            SAFE_APPEND("tsa_pid_bitrate_bps{stream_id=\"%s\",pid=\"0x%04x\",type=\"%s\"} %llu\n", sid, p, t,
                        (unsigned long long)s->pid_bitrate_bps[p]);

            if (snap->pids[j].width > 0) {
                SAFE_APPEND("tsa_video_width{stream_id=\"%s\",pid=\"0x%04x\"} %u\n", sid, p, snap->pids[j].width);
                SAFE_APPEND("tsa_video_height{stream_id=\"%s\",pid=\"0x%04x\"} %u\n", sid, p, snap->pids[j].height);
            }
        }
    }
    free(snap);
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
