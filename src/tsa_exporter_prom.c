#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void tsa_exporter_prom_v2(tsa_handle_t** handles, int count, char* buf, size_t sz) {
    if (!handles || !buf || sz < 4096) return;
    int off = 0;

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
        off += snprintf(buf + off, sz - off,
                        "# HELP tsa_global_network_incident 1 if more than 50%% streams are failing\n");
        off += snprintf(buf + off, sz - off, "tsa_global_network_incident 1\n");
    } else {
        off += snprintf(buf + off, sz - off, "tsa_global_network_incident 0\n");
    }

    for (int i = 0; i < count; i++) {
        tsa_handle_t* h = handles[i];
        if (!h) continue;

        tsa_snapshot_full_t snap;
        if (tsa_take_snapshot_full(h, &snap) != 0) continue;

        const tsa_tr101290_stats_t* s = &snap.stats;
        const char* sid = h->config.input_label;
        if (!sid[0]) sid = "unknown";

        char labels[128];
        snprintf(labels, sizeof(labels), "{stream_id=\"%s\"}", sid);

        // Tier 1: Core
        off += snprintf(buf + off, sz - off, "tsa_signal_lock_status%s %d\n", labels, snap.summary.signal_lock ? 1 : 0);
        off += snprintf(buf + off, sz - off, "tsa_health_score%s %.1f\n", labels, snap.predictive.master_health);

        // Tier 2: ETR 290 P1
        off += snprintf(buf + off, sz - off, "tsa_tr101290_p1_sync_loss%s %llu\n", labels,
                        (unsigned long long)s->sync_loss.count);
        off += snprintf(buf + off, sz - off, "tsa_tr101290_p1_pat_error%s %llu\n", labels,
                        (unsigned long long)s->pat_error.count);
        off += snprintf(buf + off, sz - off, "tsa_tr101290_p1_cc_error%s %llu\n", labels,
                        (unsigned long long)s->cc_error.count);

        off += snprintf(buf + off, sz - off, "tsa_tr101290_p1_errors{stream_id=\"%s\",error_type=\"sync_loss\"} %llu\n",
                        sid, (unsigned long long)s->sync_loss.count);
        off += snprintf(buf + off, sz - off, "tsa_tr101290_p1_errors{stream_id=\"%s\",error_type=\"pat_error\"} %llu\n",
                        sid, (unsigned long long)s->pat_error.count);
        off += snprintf(buf + off, sz - off, "tsa_tr101290_p1_errors{stream_id=\"%s\",error_type=\"cc_error\"} %llu\n",
                        sid, (unsigned long long)s->cc_error.count);

        // Compatibility names for older tests
        off += snprintf(buf + off, sz - off, "tsa_continuity_errors_total%s %llu\n", labels,
                        (unsigned long long)s->cc_error.count);
        off += snprintf(buf + off, sz - off, "tsa_sync_byte_errors_total%s %llu\n", labels,
                        (unsigned long long)s->sync_byte_error.count);
        off += snprintf(buf + off, sz - off, "tsa_pcr_jitter_ms%s %.3f\n", labels, s->pcr_jitter_avg_ns / 1000000.0);

        // Document Aligned Metrics
        off += snprintf(buf + off, sz - off, "tsa_sync_loss_errors%s %llu\n", labels,
                        (unsigned long long)s->sync_loss.count);
        off += snprintf(buf + off, sz - off, "tsa_pat_error_count%s %llu\n", labels,
                        (unsigned long long)s->pat_error.count);
        off += snprintf(buf + off, sz - off, "tsa_pmt_error_count%s %llu\n", labels,
                        (unsigned long long)s->pmt_error.count);
        off += snprintf(buf + off, sz - off, "tsa_srt_rtt_ms%s %lld\n", labels, (long long)snap.srt.rtt_ms);

        // Tier 3: Analytics
        off += snprintf(buf + off, sz - off, "tsa_physical_bitrate_bps%s %llu\n", labels,
                        (unsigned long long)s->physical_bitrate_bps);
        off += snprintf(buf + off, sz - off, "tsa_pcr_bitrate_bps%s %llu\n", labels,
                        (unsigned long long)s->pcr_bitrate_bps);
        off += snprintf(buf + off, sz - off, "tsa_mdi_delay_factor_ms%s %.2f\n", labels, (float)s->mdi_df_ms);
        off += snprintf(buf + off, sz - off, "tsa_essence_video_fps%s %.2f\n", labels, (float)s->video_fps);
        off += snprintf(buf + off, sz - off, "tsa_essence_av_sync_ms%s %d\n", labels, s->av_sync_ms);

        // Tier 4: RST
        off += snprintf(buf + off, sz - off, "tsa_rst_network_seconds%s %.2f\n", labels, snap.predictive.rst_network_s);

        // PID Details
        for (int p = 0; p < TS_PID_MAX; p++) {
            if (s->pid_packet_count[p] > 0) {
                const char* t = snap.pids[p].type_str[0] ? snap.pids[p].type_str : "Unknown";
                off += snprintf(buf + off, sz - off,
                                "tsa_pid_bitrate_bps{stream_id=\"%s\",pid=\"0x%04x\",type=\"%s\"} %llu\n", sid, p, t,
                                (unsigned long long)s->pid_bitrate_bps[p]);
                off += snprintf(buf + off, sz - off,
                                "tsa_pid_inventory_bitrate_bps{stream_id=\"%s\",pid=\"0x%04x\",type=\"%s\"} %llu\n",
                                sid, p, t, (unsigned long long)s->pid_bitrate_bps[p]);

                // Export GOP and Resolution for Video PIDs
                if (snap.pids[p].width > 0) {
                    off += snprintf(buf + off, sz - off, "tsa_video_width{stream_id=\"%s\",pid=\"0x%04x\"} %u\n", sid,
                                    p, snap.pids[p].width);
                    off += snprintf(buf + off, sz - off, "tsa_video_height{stream_id=\"%s\",pid=\"0x%04x\"} %u\n", sid,
                                    p, snap.pids[p].height);
                    if (snap.pids[p].gop_n > 0) {
                        off += snprintf(buf + off, sz - off, "tsa_video_gop_n{stream_id=\"%s\",pid=\"0x%04x\"} %u\n",
                                        sid, p, snap.pids[p].gop_n);
                        off += snprintf(buf + off, sz - off, "tsa_video_gop_ms{stream_id=\"%s\",pid=\"0x%04x\"} %u\n",
                                        sid, p, snap.pids[p].gop_ms);
                    }
                }
            }
        }
    }
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
