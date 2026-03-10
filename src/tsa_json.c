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
    SAFE_JSON("\"status\":{\"signal_lock\":%s,\"master_health\":%.1f},", sn->summary.signal_lock ? "true" : "false",
              sn->predictive.master_health);

    SAFE_JSON("\"tier1_link\":{\"physical_bitrate_bps\":%llu,\"mdi_df_ms\":%.2f},",
              (unsigned long long)st->physical_bitrate_bps, (float)st->mdi_df_ms);

    SAFE_JSON(
        "\"tier2_compliance\":{\"p1\":{\"cc_error\":{\"count\":%llu,\"last_offset\":%llu}},\"p2\":{\"pcr_jitter_ms\":%."
        "3f,\"tsa_compliance_"
        "pcr_repetition_errors\": %llu}},",
        (unsigned long long)st->cc_error.count, (unsigned long long)st->cc_error.absolute_byte_offset,
        st->pcr_jitter_avg_ns / 1000000.0, (unsigned long long)st->pcr_repetition_error.count);

    SAFE_JSON("\"pids\":[");
    for (uint32_t i = 0; i < sn->active_pid_count; i++) {
        const tsa_pid_info_t* p = &sn->pids[i];
        SAFE_JSON("%s{\"pid\":\"0x%04x\",\"type\":\"%s\",\"bitrate_bps\":%llu,\"width\":%u,\"height\":%u,\"fps\":%.2f}",
                  (i == 0) ? "" : ",", p->pid, tsa_get_pid_type_name(h, p->pid),
                  (unsigned long long)sn->stats.pid_bitrate_bps[p->pid], p->width, p->height, p->exact_fps);
    }
    SAFE_JSON("]}");

#undef SAFE_JSON
    return (size_t)off;
}
