#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/* Helper to ensure Prometheus metrics are never NaN or Inf */
static double safe_val(double v) {
    return isfinite(v) ? v : 0.0;
}

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
    if (valid_streams > 0 && (double)alarmed_streams / valid_streams > 0.5) {
        if (valid_streams >= 3) {
            SAFE_APPEND("# HELP tsa_system_global_incident 1 if more than 50%% streams are failing\n");
            SAFE_APPEND("tsa_system_global_incident 1\n");
        }
    } else {
        SAFE_APPEND("tsa_system_global_incident 0\n");
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

        char labels[TSA_LABEL_MAX];
        snprintf(labels, sizeof(labels), "{stream_id=\"%s\"}", sid);

        // Tier 1: System & Engine Status
        SAFE_APPEND("tsa_system_signal_locked%s %d\n", labels, snap->summary.signal_lock ? 1 : 0);
        SAFE_APPEND("tsa_system_health_score%s %.1f\n", labels, (float)safe_val(snap->predictive.master_health));
        SAFE_APPEND("tsa_system_internal_drop_count%s %llu\n", labels, (unsigned long long)s->internal_analyzer_drop);
        SAFE_APPEND("tsa_system_worker_overruns%s %llu\n", labels, (unsigned long long)s->worker_slice_overruns);
        SAFE_APPEND("tsa_system_engine_latency_ns%s %llu\n", labels,
                    (unsigned long long)s->engine_processing_latency_ns);

        // Tier 2: Transport & Link Layer
        SAFE_APPEND("tsa_transport_srt_rtt_ms%s %lld\n", labels, (long long)snap->srt.rtt_ms);
        SAFE_APPEND("tsa_transport_srt_retransmit_rate%s %.4f\n", labels, (float)safe_val(snap->srt.retransmit_tax));
        SAFE_APPEND("tsa_transport_mdi_mlr_pkts_s%s %.2f\n", labels, (float)safe_val(s->mdi_mlr_pkts_s));
        SAFE_APPEND("tsa_transport_mdi_df_ms%s %.2f\n", labels, (float)safe_val(s->mdi_df_ms));

        SAFE_APPEND("tsa_transport_iat_histogram{stream_id=\"%s\",le=\"1ms\"} %llu\n", sid,
                    (unsigned long long)s->iat_hist.bucket_under_1ms);
        SAFE_APPEND("tsa_transport_iat_histogram{stream_id=\"%s\",le=\"2ms\"} %llu\n", sid,
                    (unsigned long long)(s->iat_hist.bucket_under_1ms + s->iat_hist.bucket_1_2ms));
        SAFE_APPEND(
            "tsa_transport_iat_histogram{stream_id=\"%s\",le=\"5ms\"} %llu\n", sid,
            (unsigned long long)(s->iat_hist.bucket_under_1ms + s->iat_hist.bucket_1_2ms + s->iat_hist.bucket_2_5ms));
        SAFE_APPEND("tsa_transport_iat_histogram{stream_id=\"%s\",le=\"10ms\"} %llu\n", sid,
                    (unsigned long long)(s->iat_hist.bucket_under_1ms + s->iat_hist.bucket_1_2ms +
                                         s->iat_hist.bucket_2_5ms + s->iat_hist.bucket_5_10ms));
        SAFE_APPEND(
            "tsa_transport_iat_histogram{stream_id=\"%s\",le=\"100ms\"} %llu\n", sid,
            (unsigned long long)(s->iat_hist.bucket_under_1ms + s->iat_hist.bucket_1_2ms + s->iat_hist.bucket_2_5ms +
                                 s->iat_hist.bucket_5_10ms + s->iat_hist.bucket_10_100ms));
        SAFE_APPEND("tsa_transport_iat_histogram{stream_id=\"%s\",le=\"+Inf\"} %llu\n", sid,
                    (unsigned long long)(s->iat_hist.bucket_under_1ms + s->iat_hist.bucket_1_2ms +
                                         s->iat_hist.bucket_2_5ms + s->iat_hist.bucket_5_10ms +
                                         s->iat_hist.bucket_10_100ms + s->iat_hist.bucket_over_100ms));

        // Tier 3: Compliance (TR 101 290 & PCR)
        SAFE_APPEND("tsa_tr101290_p1_sync_loss_total%s %llu\n", labels, (unsigned long long)s->sync_loss_count);
        SAFE_APPEND("tsa_tr101290_p1_sync_byte_errors_total%s %llu\n", labels,
                    (unsigned long long)s->sync_byte_error_count);
        SAFE_APPEND("tsa_tr101290_p1_pat_errors_total%s %llu\n", labels, (unsigned long long)s->pat_error_count);
        SAFE_APPEND("tsa_tr101290_p1_pmt_errors_total%s %llu\n", labels, (unsigned long long)s->pmt_error_count);
        SAFE_APPEND("tsa_tr101290_p1_cc_errors_total%s %llu\n", labels, (unsigned long long)s->cc_error.count);
        SAFE_APPEND("tsa_tr101290_p1_pid_errors_total%s %llu\n", labels, (unsigned long long)s->pid_error_count);

        SAFE_APPEND("tsa_tr101290_p2_tei_errors_total%s %llu\n", labels, (unsigned long long)s->tei_error_count);
        SAFE_APPEND("tsa_tr101290_p2_pts_errors_total%s %llu\n", labels, (unsigned long long)s->pts_error.count);
        SAFE_APPEND("tsa_tr101290_p2_crc_errors_total%s %llu\n", labels, (unsigned long long)s->crc_error.count);
        SAFE_APPEND("tsa_tr101290_p2_transport_errors_total%s %llu\n", labels,
                    (unsigned long long)s->transport_error.count);

        SAFE_APPEND("tsa_tr101290_p3_sdt_errors_total%s %llu\n", labels, (unsigned long long)s->sdt_error_count);
        SAFE_APPEND("tsa_tr101290_p3_nit_errors_total%s %llu\n", labels, (unsigned long long)s->nit_error_count);
        SAFE_APPEND("tsa_tr101290_p3_eit_errors_total%s %llu\n", labels, (unsigned long long)s->eit_error_count);
        SAFE_APPEND("tsa_tr101290_p3_tdt_errors_total%s %llu\n", labels, (unsigned long long)s->tdt_error_count);

        SAFE_APPEND("tsa_tr101290_errors{stream_id=\"%s\",error_type=\"sync_loss\"} %llu\n", sid,
                    (unsigned long long)s->sync_loss_count);
        SAFE_APPEND("tsa_compliance_tr101290_errors_last_offset{stream_id=\"%s\",error_type=\"sync_loss\"} %llu\n", sid,
                    (unsigned long long)s->sync_loss.absolute_byte_offset);
        SAFE_APPEND("tsa_compliance_tr101290_errors{stream_id=\"%s\",error_type=\"pat_error\"} %llu\n", sid,
                    (unsigned long long)s->pat_error.count);
        SAFE_APPEND("tsa_compliance_tr101290_errors_last_offset{stream_id=\"%s\",error_type=\"pat_error\"} %llu\n", sid,
                    (unsigned long long)s->pat_error.absolute_byte_offset);
        SAFE_APPEND("tsa_compliance_tr101290_errors{stream_id=\"%s\",error_type=\"cc_error\"} %llu\n", sid,
                    (unsigned long long)s->cc_error.count);
        SAFE_APPEND("tsa_compliance_tr101290_errors_last_offset{stream_id=\"%s\",error_type=\"cc_error\"} %llu\n", sid,
                    (unsigned long long)s->cc_error.absolute_byte_offset);
        SAFE_APPEND("tsa_compliance_tr101290_errors{stream_id=\"%s\",error_type=\"pmt_error\"} %llu\n", sid,
                    (unsigned long long)s->pmt_error.count);
        SAFE_APPEND("tsa_compliance_tr101290_errors_last_offset{stream_id=\"%s\",error_type=\"pmt_error\"} %llu\n", sid,
                    (unsigned long long)s->pmt_error.absolute_byte_offset);
        SAFE_APPEND("tsa_compliance_tr101290_errors{stream_id=\"%s\",error_type=\"pid_error\"} %llu\n", sid,
                    (unsigned long long)s->pid_error.count);
        SAFE_APPEND("tsa_compliance_tr101290_errors{stream_id=\"%s\",error_type=\"pts_error\"} %llu\n", sid,
                    (unsigned long long)s->pts_error.count);
        SAFE_APPEND("tsa_compliance_tr101290_errors{stream_id=\"%s\",error_type=\"crc_error\"} %llu\n", sid,
                    (unsigned long long)s->crc_error.count);
        SAFE_APPEND("tsa_compliance_tr101290_errors{stream_id=\"%s\",error_type=\"transport_error\"} %llu\n", sid,
                    (unsigned long long)s->transport_error.count);

        SAFE_APPEND("tsa_tr101290_pcr_repetition_errors%s %llu\n", labels,
                    (unsigned long long)s->pcr_repetition_error.count);

        SAFE_APPEND("tsa_tr101290_pcr_accuracy_errors%s %llu\n", labels,
                    (unsigned long long)s->pcr_accuracy_error.count);

        // Tier 3.1: Active Alerts Status
        for (int k = 0; k < TSA_ALERT_MAX; k++) {
            const char* alert_name = tsa_alert_get_name((tsa_alert_id_t)k);
            if (strcmp(alert_name, "UNKNOWN") == 0) continue;
            uint64_t mask = tsa_alert_get_mask((tsa_alert_id_t)k);
            int is_firing = (s->active_alerts_mask & mask) ? 1 : 0;
            SAFE_APPEND("tsa_alert_status{stream_id=\"%s\",alert_id=\"%s\"} %d\n", sid, alert_name, is_firing);
        }

        // Tier 4: Metrology & Timing
        SAFE_APPEND("tsa_metrology_physical_bitrate_bps%s %llu\n", labels, (unsigned long long)s->physical_bitrate_bps);
        SAFE_APPEND("tsa_metrology_pcr_bitrate_bps%s %llu\n", labels, (unsigned long long)s->pcr_bitrate_bps);
        SAFE_APPEND("tsa_metrology_pcr_bitrate_piecewise_bps%s %llu\n", labels,
                    (unsigned long long)s->last_pcr_interval_bitrate_bps);
        SAFE_APPEND("tsa_metrology_pcr_jitter_ms%s %.3f\n", labels, safe_val(s->pcr_jitter_avg_ns) / 1000000.0);
        SAFE_APPEND("tsa_metrology_pcr_accuracy_ns%s %.2f\n", labels, (float)safe_val(s->pcr_accuracy_ns));
        SAFE_APPEND("tsa_metrology_pcr_accuracy_piecewise_ms%s %.3f\n", labels,
                    safe_val(s->pcr_accuracy_ns_piecewise) / 1000000.0);
        SAFE_APPEND("tsa_metrology_stc_wall_drift_ppm%s %.3f\n", labels, safe_val(snap->predictive.stc_wall_drift_ppm));
        SAFE_APPEND("tsa_metrology_long_term_drift_ppm%s %.3f\n", labels,
                    safe_val(snap->predictive.long_term_drift_ppm));

        // Tier 5: Essence & Quality
        SAFE_APPEND("tsa_essence_video_fps%s %.2f\n", labels, (float)safe_val(s->video_fps));
        SAFE_APPEND("tsa_essence_gop_ms%s %u\n", labels, s->gop_ms);
        SAFE_APPEND("tsa_essence_av_sync_ms%s %d\n", labels, s->av_sync_ms);
        SAFE_APPEND("tsa_essence_entropy_freeze_total%s %llu\n", labels, (unsigned long long)s->entropy_freeze.count);

        // Tier 6: Predictive Simulation
        SAFE_APPEND("tsa_predictive_rst_network_seconds%s %.2f\n", labels,
                    (float)safe_val(snap->predictive.rst_network_s));
        SAFE_APPEND("tsa_predictive_tstd_underflow_total%s %llu\n", labels,
                    (unsigned long long)s->tstd_underflow.count);
        SAFE_APPEND("tsa_predictive_tstd_overflow_total%s %llu\n", labels, (unsigned long long)s->tstd_overflow.count);

        // PID Details
        for (uint32_t j = 0; j < snap->active_pid_count; j++) {
            uint16_t p = snap->pids[j].pid;
            const char* pid_labels = h->pid_labels[p];

            SAFE_APPEND("tsa_metrology_pid_bitrate_bps%s %llu\n", pid_labels,
                        (unsigned long long)s->pid_bitrate_bps[p]);
            SAFE_APPEND("tsa_metrology_pid_bitrate_peak_bps%s %llu\n", pid_labels,
                        (unsigned long long)s->pid_bitrate_peak_bps[p]);
            SAFE_APPEND("tsa_metrology_pid_bitrate_min_bps%s %llu\n", pid_labels,
                        (unsigned long long)s->pid_bitrate_min_bps[p]);
            SAFE_APPEND("tsa_metrology_pid_scrambled_packets_total%s %llu\n", pid_labels,
                        (unsigned long long)s->pid_scrambled_packets[p]);
            SAFE_APPEND("tsa_metrology_pid_pes_errors_total%s %llu\n", pid_labels,
                        (unsigned long long)s->pid_pes_errors[p]);

            if (snap->pids[j].eb_fill_pct > 0 || snap->pids[j].tb_fill_pct > 0) {
                SAFE_APPEND("tsa_compliance_pid_tstd_eb_fill_pct%s %.2f\n", pid_labels, snap->pids[j].eb_fill_pct);
                SAFE_APPEND("tsa_compliance_pid_tstd_tb_fill_pct%s %.2f\n", pid_labels, snap->pids[j].tb_fill_pct);
                SAFE_APPEND("tsa_compliance_pid_tstd_mb_fill_pct%s %.2f\n", pid_labels, snap->pids[j].mb_fill_pct);
            }
            SAFE_APPEND("tsa_compliance_pid_has_cea708%s %d\n", pid_labels, snap->pids[j].has_cea708 ? 1 : 0);
            SAFE_APPEND("tsa_compliance_pid_has_scte35%s %d\n", pid_labels, snap->pids[j].has_scte35 ? 1 : 0);
            SAFE_APPEND("tsa_compliance_pid_is_closed_gop%s %d\n", pid_labels, snap->pids[j].is_closed_gop ? 1 : 0);
            SAFE_APPEND("tsa_essence_pid_closed_gops_total%s %llu\n", pid_labels,
                        (unsigned long long)snap->pids[j].closed_gops);
            SAFE_APPEND("tsa_essence_pid_open_gops_total%s %llu\n", pid_labels,
                        (unsigned long long)snap->pids[j].open_gops);

            if (snap->pids[j].width > 0) {
                char v_labels[TSA_LABEL_MAX];
                snprintf(v_labels, sizeof(v_labels), "{stream_id=\"%s\",pid=\"0x%04x\"}", sid, p);
                SAFE_APPEND("tsa_essence_pid_video_width%s %u\n", v_labels, snap->pids[j].width);
                SAFE_APPEND("tsa_essence_pid_video_height%s %u\n", v_labels, snap->pids[j].height);
                SAFE_APPEND("tsa_essence_pid_video_profile%s %u\n", v_labels, snap->pids[j].profile);
                SAFE_APPEND("tsa_essence_pid_video_level%s %u\n", v_labels, snap->pids[j].level);
                SAFE_APPEND("tsa_essence_pid_video_bit_depth%s %u\n", v_labels, snap->pids[j].bit_depth);
                SAFE_APPEND("tsa_essence_pid_video_chroma_format%s %u\n", v_labels, snap->pids[j].chroma_format);
                if (snap->pids[j].gop_n > 0) {
                    SAFE_APPEND("tsa_essence_pid_video_gop_n%s %u\n", v_labels, snap->pids[j].gop_n);
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

void tsa_exporter_prom_core(tsa_handle_t** handles, int count, char* buf, size_t sz) {
    if (!handles || !buf || sz < 1024) return;
    int off = 0;
    int n;

#define SAFE_APPEND(...)                                                           \
    {                                                                              \
        n = snprintf(buf + off, (sz > (size_t)off) ? (sz - off) : 0, __VA_ARGS__); \
        if (n > 0) off += (off + n < (int)sz) ? n : (int)(sz - off - 1);           \
    }

    for (int i = 0; i < count; i++) {
        tsa_handle_t* h = handles[i];
        if (!h) continue;

        tsa_snapshot_lite_t s;
        if (tsa_take_snapshot_lite(h, &s) != 0) continue;

        const char* sid = h->config.input_label;
        if (!sid[0]) sid = "unknown";

        SAFE_APPEND("tsa_system_signal_locked{stream_id=\"%s\"} %d\n", sid, s.signal_lock ? 1 : 0);
        SAFE_APPEND("tsa_system_health_score{stream_id=\"%s\"} %.1f\n", sid, s.master_health);
    }
#undef SAFE_APPEND
}

void tsa_exporter_prom_pids(tsa_handle_t** handles, int count, char* buf, size_t sz) {
    tsa_exporter_prom_v2(handles, count, buf, sz);
}

void tsa_export_pid_labels(tsa_metric_buffer_t* buf, tsa_handle_t* h, uint16_t pid) {
    if (!buf || !h || pid >= TS_PID_MAX) return;

    char label[128];
    const char* codec = tsa_get_pid_type_name(h, pid);
    const char* type = "Other";

    if (strcmp(codec, "H.264") == 0 || strcmp(codec, "HEVC") == 0 || strcmp(codec, "MPEG2-V") == 0 ||
        strcmp(codec, "MPEG1-V") == 0) {
        type = "Video";
    } else if (strcmp(codec, "AAC") == 0 || strcmp(codec, "ADTS-AAC") == 0 || strcmp(codec, "MPEG1-A") == 0 ||
               strcmp(codec, "MPEG2-A") == 0 || strcmp(codec, "AC3") == 0 || strcmp(codec, "AAC-LATM") == 0) {
        type = "Audio";
    } else if (strcmp(codec, "Subtitle") == 0 || strcmp(codec, "Teletext") == 0) {
        type = "Data";
    }

    snprintf(label, sizeof(label), "pid=\"0x%04x\",type=\"%s\",codec=\"%s\"", pid, type, codec);
    tsa_mbuf_append_str(buf, label);
}
