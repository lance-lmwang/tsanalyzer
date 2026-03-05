#include "tsa_internal.h"
#include <stdio.h>
#include <string.h>

size_t tsa_snapshot_to_json(tsa_handle_t* h, const tsa_snapshot_full_t* sn, char* b, size_t s) {
    if (!sn || !b || s < 2048) return 0;
    int off = 0;
    int n;

#define SAFE_JSON(...)                                                         \
    {                                                                          \
        n = snprintf(b + off, (s > (size_t)off) ? (s - off) : 0, __VA_ARGS__); \
        if (n > 0) off += (off + n < (int)s) ? n : (int)(s - off - 1);         \
    }

    const tsa_tr101290_stats_t* st = &sn->stats;

    SAFE_JSON("{");
    // Tier 0/1: Master Control Console (SIGNAL STATUS)
    SAFE_JSON(
        "\"status\":{\"signal_lock\":%s,\"network_name\":\"%s\",\"service_name\":\"%s\",\"provider_name\":\"%s\","
        "\"master_health\":%.1f,"
        "\"engine_determinism\":{\"drops\":%llu,\"overruns\":%llu}},",
        sn->summary.signal_lock ? "true" : "false", sn->network_name, sn->service_name, sn->provider_name,
        sn->predictive.master_health,
        (unsigned long long)st->internal_analyzer_drop, (unsigned long long)st->worker_slice_overruns);

    // Tier 2: Transport & Link Integrity (SRT/MDI)
    SAFE_JSON(
        "\"tier1_link\":{\"physical_bitrate_bps\":%llu,\"srt_rtt_ms\":%lld,\"srt_loss_p0\":%llu,\"srt_retransmit_pct\":"
        "%.2f,\"mdi_df_ms\":%.2f},",
        (unsigned long long)st->physical_bitrate_bps, (long long)sn->srt.rtt_ms, (unsigned long long)sn->srt.bytes_lost,
        sn->srt.retransmit_tax * 100.0, (float)st->mdi_df_ms);

    // Tier 3/4: ETR 290 P1 & P2
    SAFE_JSON(
        "\"tier2_compliance\":{\"p1\":{\"sync_loss\":%llu,\"pat_error\":%llu,\"cc_error\":{\"count\":%llu,\"first_occur\":"
        "%llu,\"last_occur\":%llu},\"pmt_error\":%llu,"
        "\"pid_error\":%llu},\"p2\":{\"pcr_jitter_ms\":%.3f,\"pcr_accuracy_piecewise_ms\":%.3f,\"piecewise_pcr_bitrate_"
        "bps\":%llu,\"pcr_repetition\":%llu,\"pts_error\":%llu,\"crc_error\":%llu,\"transport_error\":%llu}},",
        (unsigned long long)st->sync_loss.count, (unsigned long long)st->pat_error.count,
        (unsigned long long)st->cc_error.count, (unsigned long long)st->cc_error.first_timestamp_ns,
        (unsigned long long)st->cc_error.last_timestamp_ns, (unsigned long long)st->pmt_error.count,
        (unsigned long long)st->pid_error.count, st->pcr_jitter_avg_ns / 1000000.0,
        st->pcr_accuracy_ns_piecewise / 1000000.0, (unsigned long long)st->last_pcr_interval_bitrate_bps,
        (unsigned long long)st->pcr_repetition_error.count, (unsigned long long)st->pts_error.count,
        (unsigned long long)st->crc_error.count, (unsigned long long)st->transport_error.count);

    // Tier 5/6: Essence & Payload Dynamics
    SAFE_JSON(
        "\"tier3_essence\":{\"total_bitrate_bps\":%llu,\"video_fps\":%.2f,\"gop_ms\":%u,\"av_sync_offset_ms\":%d},",
        (unsigned long long)st->physical_bitrate_bps, (float)st->video_fps, st->gop_ms, st->av_sync_ms);

    // Tier 4/5: Predictive & RST
    SAFE_JSON(
        "\"tier4_predictive\":{\"rst_network_s\":%.2f,\"rst_encoder_s\":%.2f,\"stc_wall_drift_ppm\":%.3f,\"mdi_df_ms\":"
        "%.2f},",
        sn->predictive.rst_network_s, sn->predictive.rst_encoder_s, sn->predictive.stc_wall_drift_ppm,
        (float)st->mdi_df_ms);

    // PIDs & T-STD Details
    SAFE_JSON("\"pids\":[");
    for (uint32_t i = 0; i < sn->active_pid_count; i++) {
        const tsa_pid_info_t* p = &sn->pids[i];
        SAFE_JSON(
            "%s{\"pid\":\"0x%04x\",\"type\":\"%s\",\"bitrate_bps\":%llu,"
            "\"buffer_status\":{\"eb_fill_pct\":%.2f,\"tb_fill_pct\":%.2f,\"mb_fill_pct\":%.2f}",
            (i == 0) ? "" : ",", p->pid, tsa_get_pid_type_name(h, p->pid),
            (unsigned long long)sn->stats.pid_bitrate_bps[p->pid], p->eb_fill_pct, p->tb_fill_pct, p->mb_fill_pct);

        const char* p_st = tsa_get_pid_type_name(h, p->pid);
        if (strcmp(p_st, "H.264") == 0 || strcmp(p_st, "HEVC") == 0 || strcmp(p_st, "MPEG2-V") == 0) {
            SAFE_JSON(",\"video_metadata\":{\"width\":%u,\"height\":%u,\"profile\":%u,\"gop_n\":%u,\"gop_ms\":%u,\"has_cea708\":"
                      "%s,\"has_scte35\":%s}",
                      p->width, p->height, p->profile, p->gop_n, p->gop_ms, p->has_cea708 ? "true" : "false",
                      p->has_scte35 ? "true" : "false");
        }

        SAFE_JSON("}");
    }
    SAFE_JSON("]}");

#undef SAFE_JSON
    return (size_t)off;
}
