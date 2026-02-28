#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsp.h"

struct tsa_gateway {
    tsa_gateway_config_t cfg;
    tsa_handle_t* tsa;
    tsp_handle_t* tsp;
    uint8_t last_cc[TS_PID_MAX];
    bool pid_seen[TS_PID_MAX];
    bool bypassing;
    uint64_t last_process_ns;
    uint64_t debug_stall_ns;
};

tsa_gateway_t* tsa_gateway_create(const tsa_gateway_config_t* cfg) {
    if (!cfg) return NULL;
    tsa_gateway_t* gw = calloc(1, sizeof(tsa_gateway_t));
    if (!gw) return NULL;
    gw->cfg = *cfg;

    gw->tsa = tsa_create(&cfg->analysis);

    tsp_config_t p_cfg = {.srt_url = cfg->pacing.srt_url,
                          .dest_ip = cfg->pacing.dest_ip,
                          .port = cfg->pacing.port,
                          .bitrate = cfg->pacing.bitrate,
                          .ts_per_udp = cfg->pacing.ts_per_udp,
                          .cpu_core = -1};
    gw->tsp = tsp_create(&p_cfg);
    tsp_start(gw->tsp);

    gw->last_process_ns = 0;
    gw->bypassing = false;

    return gw;
}

void tsa_gateway_destroy(tsa_gateway_t* gw) {
    if (!gw) return;
    tsp_stop(gw->tsp);
    tsp_destroy(gw->tsp);
    tsa_destroy(gw->tsa);
    free(gw);
}

int tsa_gateway_process(tsa_gateway_t* gw, const uint8_t* pkt, uint64_t now_ns) {
    if (!gw || !pkt) return -1;

    uint64_t effective_now = now_ns + gw->debug_stall_ns;
    if (gw->last_process_ns > 0 && gw->cfg.watchdog_timeout_ns > 0) {
        if (effective_now - gw->last_process_ns > gw->cfg.watchdog_timeout_ns) {
            gw->bypassing = true;
        }
    }
    gw->last_process_ns = effective_now;

    if (gw->bypassing) {
        return tsp_enqueue(gw->tsp, pkt, 1);
    }

    tsa_process_packet(gw->tsa, pkt, now_ns);

    if (gw->cfg.enable_null_substitution) {
        uint16_t pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
        uint8_t cc = pkt[3] & 0x0F;
        if (gw->pid_seen[pid]) {
            uint8_t expected = (gw->last_cc[pid] + 1) & 0x0F;
            if (cc != expected && cc != gw->last_cc[pid]) {
                uint8_t loss_count = (cc - expected) & 0x0F;
                uint8_t null_pkt[188];
                memset(null_pkt, 0, 188);
                null_pkt[0] = 0x47;
                null_pkt[1] = 0x1F;
                null_pkt[2] = 0xFF;  // NULL PID
                for (int i = 0; i < loss_count; i++) {
                    tsp_enqueue(gw->tsp, null_pkt, 1);
                }
            }
        }
        gw->last_cc[pid] = cc;
        gw->pid_seen[pid] = true;
    }

    return tsp_enqueue(gw->tsp, pkt, 1);
}

tsa_handle_t* tsa_gateway_get_tsa_handle(tsa_gateway_t* gw) {
    return gw ? gw->tsa : NULL;
}
tsp_handle_t* tsa_gateway_get_tsp_handle(tsa_gateway_t* gw) {
    return gw ? gw->tsp : NULL;
}

bool tsa_gateway_is_bypassing(tsa_gateway_t* gw) {
    return gw ? gw->bypassing : false;
}
void tsa_gateway_debug_inject_stall(tsa_gateway_t* gw, uint64_t duration_ns) {
    if (gw) {
        gw->debug_stall_ns += duration_ns;
    }
}
