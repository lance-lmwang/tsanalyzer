#include <stdio.h>
#include <string.h>

#include "tsa_internal.h"

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
        sn->predictive.master_health, (unsigned long long)st->internal_analyzer_drop,
        (unsigned long long)st->worker_slice_overruns);

    // Tier 2: Transport & Link Integrity (SRT/MDI)
    SAFE_JSON(
        "\"tier1_link\":{\"physical_bitrate_bps\":%llu,\"srt_rtt_ms\":%lld,\"srt_loss_p0\":%llu,\"srt_retransmit_pct\":"
        "%.2f,\"mdi_df_ms\":%.2f},",
        (unsigned long long)st->physical_bitrate_bps, (long long)sn->srt.rtt_ms, (unsigned long long)sn->srt.bytes_lost,
        sn->srt.retransmit_tax * 100.0, (float)st->mdi_df_ms);

    // Tier 3/4: ETR 290 P1 & P2
    SAFE_JSON(
        "\"tier2_compliance\":{\"p1\":{\"sync_loss\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu},"
        "\"pat_error\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu},"
        "\"cc_error\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu},"
        "\"pmt_error\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu},"
        "\"pid_error\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu}},"
        "\"p2\":{\"pcr_jitter_ms\":%.3f,\"pcr_accuracy_piecewise_ms\":%.3f,\"piecewise_pcr_bitrate_bps\":%llu,"
        "\"pcr_repetition\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu},"
        "\"tsa_compliance_pcr_repetition_errors\": %llu,"
        "\"pts_error\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu},"
        "\"crc_error\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu},"
        "\"transport_error\":{\"count\":%llu,\"first_occur\":%llu,\"last_occur\":%llu}}},",
        (unsigned long long)st->sync_loss.count, (unsigned long long)st->sync_loss.first_timestamp_ns,
        (unsigned long long)st->sync_loss.last_timestamp_ns, (unsigned long long)st->pat_error.count,
        (unsigned long long)st->pat_error.first_timestamp_ns, (unsigned long long)st->pat_error.last_timestamp_ns,
        (unsigned long long)st->cc_error.count, (unsigned long long)st->cc_error.first_timestamp_ns,
        (unsigned long long)st->cc_error.last_timestamp_ns, (unsigned long long)st->pmt_error.count,
        (unsigned long long)st->pmt_error.first_timestamp_ns, (unsigned long long)st->pmt_error.last_timestamp_ns,
        (unsigned long long)st->pid_error.count, (unsigned long long)st->pid_error.first_timestamp_ns,
        (unsigned long long)st->pid_error.last_timestamp_ns, st->pcr_jitter_avg_ns / 1000000.0,
        st->pcr_accuracy_ns_piecewise / 1000000.0, (unsigned long long)st->last_pcr_interval_bitrate_bps,
        (unsigned long long)st->pcr_repetition_error.count,
        (unsigned long long)st->pcr_repetition_error.first_timestamp_ns,
        (unsigned long long)st->pcr_repetition_error.last_timestamp_ns,
        (unsigned long long)st->pcr_repetition_error.count,  // Flat key
        (unsigned long long)st->pts_error.count, (unsigned long long)st->pts_error.first_timestamp_ns,
        (unsigned long long)st->pts_error.last_timestamp_ns, (unsigned long long)st->crc_error.count,
        (unsigned long long)st->crc_error.first_timestamp_ns, (unsigned long long)st->crc_error.last_timestamp_ns,
        (unsigned long long)st->transport_error.count, (unsigned long long)st->transport_error.first_timestamp_ns,
        (unsigned long long)st->transport_error.last_timestamp_ns);

    // Tier 5/6: Essence & Payload Dynamics
    SAFE_JSON("\"tier3_essence\":{\"total_bitrate_bps\":%llu,\"video_fps\":%.2f,\"gop_ms\":%u,\"av_sync_ms\":%d},",
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
            SAFE_JSON(
                ",\"video_metadata\":{\"width\":%u,\"height\":%u,\"profile\":%u,\"level\":%u,\"chroma_format\":%u,"
                "\"bit_depth\":%u,\"exact_fps\":%.3f,\"gop_n\":%u,\"gop_ms\":%u,\"has_cea708\":"
                "%s,\"has_scte35\":%s,\"is_closed_gop\":%s,"
                "\"counters\":{\"i_frames\":%llu,\"p_frames\":%llu,\"b_frames\":%llu,\"closed_gops\":%llu,\"open_"
                "gops\":%llu}}",
                p->width, p->height, p->profile, p->level, p->chroma_format, p->bit_depth, p->exact_fps, p->gop_n,
                p->gop_ms, p->has_cea708 ? "true" : "false", p->has_scte35 ? "true" : "false",
                p->is_closed_gop ? "true" : "false", (unsigned long long)p->i_frames, (unsigned long long)p->p_frames,
                (unsigned long long)p->b_frames, (unsigned long long)p->closed_gops, (unsigned long long)p->open_gops);
        }

        SAFE_JSON("}");
    }
    SAFE_JSON("]}");

#undef SAFE_JSON
    return (size_t)off;
}
