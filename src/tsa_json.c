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

    SAFE_JSON("{\n");
    SAFE_JSON("  \"status\": {\n");
    SAFE_JSON("    \"signal_lock\": %s,\n", sn->summary.signal_lock ? "true" : "false");
    SAFE_JSON("    \"master_health\": %.1f\n", sn->predictive.master_health);
    SAFE_JSON("  },\n");

    SAFE_JSON("  \"tier1_link\": {\n");
    SAFE_JSON("    \"physical_bitrate_bps\": %llu,\n", (unsigned long long)st->physical_bitrate_bps);
    SAFE_JSON("    \"mdi_df_ms\": %.2f,\n", (float)st->mdi_df_ms);
    SAFE_JSON("    \"hls_buffer_ms\": %.2f,\n", st->hls_buffer_ms);
    SAFE_JSON("    \"hls_download_errors\": %llu\n", (unsigned long long)st->hls_download_errors);
    SAFE_JSON("  },\n");

    SAFE_JSON("  \"tr101290\": {\n");

    /* Priority 1: Basic Ingest Integrity */
    SAFE_JSON("    \"p1\": {\n");
    SAFE_JSON("      \"ts_sync_loss\": %llu,\n", (unsigned long long)st->sync_loss_count);
    SAFE_JSON("      \"sync_byte_error\": %llu,\n", (unsigned long long)st->sync_byte_error_count);
    SAFE_JSON("      \"pat_error\": %llu,\n", (unsigned long long)st->pat_error_count);
    SAFE_JSON("      \"continuity_count_error\": %llu,\n", (unsigned long long)st->cc_error.count);
    SAFE_JSON("      \"pmt_error\": %llu,\n", (unsigned long long)st->pmt_error_count);
    SAFE_JSON("      \"pid_error\": %llu\n", (unsigned long long)st->pid_error_count);
    SAFE_JSON("    },\n");

    /* Priority 2: Transport & Timing */
    SAFE_JSON("    \"p2\": {\n");
    SAFE_JSON("      \"transport_error_indicator\": %llu,\n", (unsigned long long)st->tei_error_count);
    SAFE_JSON("      \"crc_error\": %llu,\n", (unsigned long long)st->crc_error.count);
    SAFE_JSON("      \"pcr_repetition_error\": %llu,\n", (unsigned long long)st->pcr_repetition_error.count);
    SAFE_JSON("      \"pcr_repetition_max_ms\": %.2f,\n", (float)st->pcr_repetition_max_ms);
    SAFE_JSON("      \"pcr_accuracy_peak_ms\": %.3f,\n", st->pcr_jitter_max_ns / 1000000.0);
    SAFE_JSON("      \"pcr_drift_ppm\": %.2f,\n", st->pcr_drift_ppm);
    SAFE_JSON("      \"jitter_histogram_ms\": {\n");
    SAFE_JSON(
        "        \"<0.0005\": %llu, \"0.001\": %llu, \"0.005\": %llu, \"0.01\": %llu, \"0.1\": %llu, \"1.0\": %llu, "
        "\"10.0\": %llu, \">10.0\": %llu\n",
        (unsigned long long)st->pcr_jitter_hist.bucket_under_500ns,
        (unsigned long long)st->pcr_jitter_hist.bucket_500ns_1us,
        (unsigned long long)st->pcr_jitter_hist.bucket_1us_5us, (unsigned long long)st->pcr_jitter_hist.bucket_5us_10us,
        (unsigned long long)st->pcr_jitter_hist.bucket_10us_100us,
        (unsigned long long)st->pcr_jitter_hist.bucket_100us_1ms,
        (unsigned long long)st->pcr_jitter_hist.bucket_1ms_10ms,
        (unsigned long long)st->pcr_jitter_hist.bucket_over_10ms);
    SAFE_JSON("      }\n");
    SAFE_JSON("    },\n");

    /* Priority 3: SI/PSI Tables */
    SAFE_JSON("    \"p3\": {\n");
    SAFE_JSON("      \"nit_error\": %llu,\n", (unsigned long long)st->nit_error_count);
    SAFE_JSON("      \"sdt_error\": %llu,\n", (unsigned long long)st->sdt_error_count);
    SAFE_JSON("      \"eit_error\": %llu,\n", (unsigned long long)st->eit_error_count);
    SAFE_JSON("      \"tdt_error\": %llu\n", (unsigned long long)st->tdt_error_count);
    SAFE_JSON("    }\n");
    SAFE_JSON("  },\n");

    SAFE_JSON("  \"observability\": {\n");
    SAFE_JSON("    \"alert_suppression_count\": %llu\n", (unsigned long long)st->alert_suppression_count);
    SAFE_JSON("  },\n");

    SAFE_JSON("  \"pids\": [\n");
    for (uint32_t i = 0; i < sn->active_pid_count; i++) {
        const tsa_pid_info_t* p = &sn->pids[i];
        SAFE_JSON("    {\n");
        SAFE_JSON("      \"pid\": \"0x%04x\",\n", p->pid);
        SAFE_JSON("      \"type\": \"%s\",\n", tsa_get_pid_type_name(h, p->pid));
        SAFE_JSON("      \"bitrate_bps\": %llu,\n", (unsigned long long)sn->stats.pid_bitrate_bps[p->pid]);
        if (p->width > 0) {
            SAFE_JSON("      \"resolution\": \"%ux%u\",\n", p->width, p->height);
            SAFE_JSON("      \"fps\": %.2f,\n", p->exact_fps);
            SAFE_JSON("      \"gop\": \"%s\",\n", p->gop_structure[0] ? p->gop_structure : "unknown");
        }
        SAFE_JSON("      \"cc_errors\": %llu\n", (unsigned long long)p->cc_errors);
        SAFE_JSON("    }%s\n", (i == sn->active_pid_count - 1) ? "" : ",");
    }
    SAFE_JSON("  ],\n");

    SAFE_JSON("  \"programs\": [\n");
    for (uint32_t i = 0; i < h->ts_model.program_count; i++) {
        const tsa_program_model_t* p = &h->ts_model.programs[i];
        SAFE_JSON("    {\n");
        SAFE_JSON("      \"program\": %u,\n", p->program_number);
        SAFE_JSON("      \"pmt_pid\": %u,\n", p->pmt_pid);
        SAFE_JSON("      \"service\": \"%s\",\n", p->service_name[0] ? p->service_name : "unknown");
        SAFE_JSON("      \"provider\": \"%s\"\n", p->provider_name[0] ? p->provider_name : "unknown");
        SAFE_JSON("    }%s\n", (i == h->ts_model.program_count - 1) ? "" : ",");
    }
    SAFE_JSON("  ]\n");
    SAFE_JSON("}\n");

#undef SAFE_JSON
    return (size_t)off;
}
