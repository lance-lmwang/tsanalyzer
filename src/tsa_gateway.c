#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsp.h"

struct tsa_gateway {
    tsa_gateway_config_t cfg;
    tsa_handle_t* tsa;
    tsp_handle_t* tsp;
    tsa_packet_ring_t* ring;
    uint8_t last_cc[TS_PID_MAX];
    bool pid_seen[TS_PID_MAX];
    bool bypassing;
    bool is_throttling;
    uint64_t last_process_ns;
    uint64_t last_rst_check_ns;
    uint64_t debug_stall_ns;
};

tsa_gateway_t* tsa_gateway_create(const tsa_gateway_config_t* cfg) {
    if (!cfg) return NULL;
    tsa_gateway_t* gw = calloc(1, sizeof(tsa_gateway_t));
    if (!gw) return NULL;
    gw->cfg = *cfg;

    gw->tsa = tsa_create(&cfg->analysis);
    gw->tsp = tsp_create(&cfg->pacing);
    if (gw->tsp) tsp_start(gw->tsp);

    if (cfg->enable_auto_forensics) {
        gw->ring = tsa_packet_ring_create(cfg->forensic_ring_size ? cfg->forensic_ring_size : 10000);
    }

    gw->last_process_ns = 0;
    gw->bypassing = false;
    return gw;
}

void tsa_gateway_destroy(tsa_gateway_t* gw) {
    if (!gw) return;
    if (gw->tsp) {
        tsp_stop(gw->tsp);
        tsp_destroy(gw->tsp);
    }
    tsa_destroy(gw->tsa);
    if (gw->ring) tsa_packet_ring_destroy(gw->ring);
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

    if (gw->bypassing) return tsp_enqueue(gw->tsp, pkt, 1);

    tsa_process_packet(gw->tsa, pkt, now_ns);

    if (gw->ring) tsa_packet_ring_push(gw->ring, pkt, now_ns);

    if (now_ns - gw->last_rst_check_ns > 100000000ULL) {
        tsa_snapshot_lite_t snap;
        if (tsa_take_snapshot_lite(gw->tsa, &snap) == 0) {
            if (snap.rst_network_s < 5.0 && !gw->is_throttling) {
                tsp_update_bitrate(gw->tsp, (uint64_t)(gw->cfg.pacing.bitrate * 0.9));
                gw->is_throttling = true;
            } else if (snap.rst_network_s > 10.0 && gw->is_throttling) {
                tsp_update_bitrate(gw->tsp, gw->cfg.pacing.bitrate);
                gw->is_throttling = false;
            }

            if (gw->cfg.enable_dynamic_grooming && snap.physical_bitrate_bps > 0) {
                // Smoothly track input bitrate
                tsp_update_bitrate(gw->tsp, snap.physical_bitrate_bps);
            }

            if (gw->cfg.enable_auto_forensics && tsa_forensic_trigger(gw->tsa, 0)) {
                char fname[64];
                snprintf(fname, sizeof(fname), "forensic_%lu.ts", (unsigned long)time(NULL));
                tsa_forensic_writer_t* tmp_w = tsa_forensic_writer_create(gw->ring, fname);
                if (tmp_w) {
                    tsa_forensic_writer_write_all(tmp_w);
                    tsa_forensic_writer_destroy(tmp_w);
                }
            }
        }
        gw->last_rst_check_ns = now_ns;
    }

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
                null_pkt[2] = 0xFF;
                for (int i = 0; i < loss_count; i++) tsp_enqueue(gw->tsp, null_pkt, 1);
            }
        }
        gw->last_cc[pid] = cc;
        gw->pid_seen[pid] = true;
    }

    gw->last_process_ns = effective_now;
    return tsp_enqueue(gw->tsp, pkt, 1);
}

tsa_handle_t* tsa_gateway_get_tsa_handle(tsa_gateway_t* gw) {
    return gw ? gw->tsa : NULL;
}
struct tsp_handle* tsa_gateway_get_tsp_handle(tsa_gateway_t* gw) {
    return (struct tsp_handle*)(gw ? gw->tsp : NULL);
}
bool tsa_gateway_is_bypassing(tsa_gateway_t* gw) {
    return gw ? gw->bypassing : false;
}
void tsa_gateway_debug_inject_stall(tsa_gateway_t* gw, uint64_t duration_ns) {
    if (gw) gw->debug_stall_ns += duration_ns;
}
