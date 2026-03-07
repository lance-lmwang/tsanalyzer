#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsp.h"

#ifndef VIDEO_PID
#define VIDEO_PID 0x0100
#endif

struct tsa_gateway {
    tsa_gateway_config_t cfg;
    tsa_handle_t* tsa;
    tsa_handle_t* tsa_backup;
    tsp_handle_t* tsp;
    tsa_packet_ring_t* ring;
    uint8_t last_cc[TS_PID_MAX];
    bool pid_seen[TS_PID_MAX];
    bool bypassing;
    bool is_throttling;
    uint64_t last_process_ns;
    uint64_t last_rst_check_ns;
    uint64_t debug_stall_ns;

    uint32_t active_index;
    uint64_t last_switch_ns;
    bool pending_discontinuity;
};

tsa_gateway_t* tsa_gateway_create(const tsa_gateway_config_t* cfg) {
    if (!cfg) return NULL;
    tsa_gateway_t* gw = calloc(1, sizeof(tsa_gateway_t));
    if (!gw) return NULL;
    gw->cfg = *cfg;

    gw->tsa = tsa_create(&cfg->analysis_primary);
    if (cfg->enable_smart_failover) {
        gw->tsa_backup = tsa_create(&cfg->analysis_backup);
    }
    gw->tsp = tsp_create(&cfg->pacing);
    if (gw->tsp) tsp_start(gw->tsp);

    if (cfg->enable_auto_forensics) {
        gw->ring = tsa_packet_ring_create(cfg->forensic_ring_size ? cfg->forensic_ring_size : 10000);
    }

    gw->active_index = 0; /* Primary */
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
    if (gw->tsa_backup) tsa_destroy(gw->tsa_backup);
    if (gw->ring) tsa_packet_ring_destroy(gw->ring);
    free(gw);
}

int tsa_gateway_process_dual(tsa_gateway_t* gw, const uint8_t* pkt_p, const uint8_t* pkt_b, uint64_t now_ns) {
    if (!gw) return -1;

    /* Always analyze both streams if they are present */
    if (pkt_p) tsa_process_packet(gw->tsa, pkt_p, now_ns);
    if (pkt_b && gw->tsa_backup) tsa_process_packet(gw->tsa_backup, pkt_b, now_ns);

    /* Health assessment and switching logic */
    if (gw->cfg.enable_smart_failover && (now_ns - gw->last_rst_check_ns > 100000000ULL)) {
        tsa_snapshot_full_t snap_p, snap_b;
        int res_p = tsa_take_snapshot_full(gw->tsa, &snap_p);
        int res_b = gw->tsa_backup ? tsa_take_snapshot_full(gw->tsa_backup, &snap_b) : -1;

        if (res_p == 0) {
            float health_p = snap_p.summary.master_health;
            float health_b = (res_b == 0) ? snap_b.summary.master_health : 0.0f;

            uint32_t target_index = gw->active_index;

            if (gw->cfg.failover_mode == TSA_FAILOVER_MODE_FORCE_PRIMARY) target_index = 0;
            else if (gw->cfg.failover_mode == TSA_FAILOVER_MODE_FORCE_BACKUP) target_index = 1;
            else if (gw->cfg.failover_mode == TSA_FAILOVER_MODE_AUTO) {
                if (gw->active_index == 0 && health_p < gw->cfg.health_threshold && health_b > health_p) {
                    target_index = 1;
                } else if (gw->active_index == 1 && health_b < gw->cfg.health_threshold && health_p > health_b) {
                    target_index = 0;
                }
            }

            if (target_index != gw->active_index && (now_ns - gw->last_switch_ns > (uint64_t)gw->cfg.switch_hold_ms * 1000000ULL)) {
                gw->active_index = target_index;
                gw->last_switch_ns = now_ns;
                gw->pending_discontinuity = true;
                printf("GATEWAY: Switched active source to %s (P:%.2f, B:%.2f)\n",
                       target_index == 0 ? "PRIMARY" : "BACKUP", health_p, health_b);

                if (gw->tsa->webhook) {
                    char switch_msg[256];
                    snprintf(switch_msg, sizeof(switch_msg), "Source Switched to %s (PrimaryHealth:%.2f, BackupHealth:%.2f)",
                             target_index == 0 ? "PRIMARY" : "BACKUP", health_p, health_b);
                    tsa_webhook_push_event(gw->tsa->webhook, gw->cfg.analysis_primary.input_label, "FAILOVER", switch_msg);
                }
            }
        }
        gw->last_rst_check_ns = now_ns;
    }

    const uint8_t* active_pkt = (gw->active_index == 0) ? pkt_p : pkt_b;
    if (!active_pkt) return 0; /* No data on active path */

    uint8_t output_pkt[188];
    memcpy(output_pkt, active_pkt, 188);

    if (gw->pending_discontinuity) {
        /* Set discontinuity indicator in adaptation field if present, or create one */
        if (output_pkt[3] & 0x20) { /* Has adaptation field */
            if (output_pkt[4] > 0) output_pkt[5] |= 0x80; /* Set bit 7: discontinuity_indicator */
        }
        gw->pending_discontinuity = false;
    }

    return tsp_enqueue(gw->tsp, output_pkt, 1);
}

int tsa_gateway_process(tsa_gateway_t* gw, const uint8_t* pkt, uint64_t now_ns) {
    return tsa_gateway_process_dual(gw, pkt, NULL, now_ns);
}

uint32_t tsa_gateway_get_active_index(tsa_gateway_t* gw) {
    return gw ? gw->active_index : 0;
}

tsa_handle_t* tsa_gateway_get_tsa_handle_backup(tsa_gateway_t* gw) {
    return gw ? gw->tsa_backup : NULL;
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
