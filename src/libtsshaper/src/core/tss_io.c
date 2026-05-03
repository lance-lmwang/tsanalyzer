#include "tss_internal.h"
#include <inttypes.h>
#include <string.h>

tss_pid_state_t *tss_get_state(tsshaper_t *ctx, int stream_index)
{
    if (stream_index < 0 || stream_index >= ctx->cfg.nb_streams)
        return NULL;
    return ctx->stream_index_to_pid[stream_index];
}

void tss_internal_enqueue_packet(tsshaper_t *ctx, tss_pid_state_t *pid, const uint8_t *packet)
{
    if (!ctx || !pid)
        return;

    pid->rx_bytes_total += TSS_TS_PACKET_SIZE;

    if (pid->ts_fifo) {
        int current_size = tss_fifo_size(pid->ts_fifo);
        int has_af = (packet[3] & 0x20);
        int af_len = packet[4];
        int is_pcr = has_af && (af_len >= 7) && (packet[5] & 0x10);
        int is_psi = (pid->type == TSS_PID_PSI);
        int is_pusi = (packet[1] & 0x40);

        int force_drop = 0;
        if (pid->type == TSS_PID_VIDEO && pid->state == TSS_STATE_WAIT_IDR && !is_pcr) {
            /* P1 Watchdog: Fallback after 3 seconds of waiting for IDR */
            if (pid->wait_idr_start_stc == 0) {
                pid->wait_idr_start_stc = ctx->v_stc;
            } else if (ctx->v_stc - pid->wait_idr_start_stc > 3LL * TSS_SYS_CLOCK_FREQ) {
                tss_log(ctx, TSS_LOG_WARN, "[T-STD] PID 0x%04x: WAIT_IDR Watchdog Timeout (3s). Forcing hard recovery.\n", pid->pid);
                pid->state = TSS_STATE_NORMAL;
                pid->wait_idr_start_stc = 0;
                ctx->dts_epoch_invalid = 1;
                pid->need_resync = 1;
                /* P1: Prevent pseudo-recovery. Force decoder barrier. */
                pid->needs_discontinuity = 1;
                pid->tel_watchdog_count++;
            } else {
                force_drop = 1;
            }
        }

        /* P1: Prioritized Shedding Strategy */
        if (!force_drop && !is_pcr && !is_psi && current_size > pid->fifo_capacity * 9 / 10) {
            if (pid->type == TSS_PID_VIDEO) {
                /* AU-aware shedding: Initiate drop only at AU boundaries (PUSI) to prevent corrupting partial frames.
                 * P0: Single Source of Truth for Keyframe. We rely on the synchronously updated is_current_au_key. */
                if (is_pusi && !pid->is_current_au_key) {
                    force_drop = 1;
                    pid->state = TSS_STATE_WAIT_IDR;
                    pid->wait_idr_start_stc = ctx->v_stc;
                    pid->need_resync = 1;
                }
            } else if (pid->type == TSS_PID_AUDIO) {
                /* P1: Refined Audio Shedding. Only drop if buffer is critically full (>95%) AND it's NOT a header/PES start.
                 * This protects ADTS/AC3 syncframes which are aligned with PUSI in standard muxers. */
                if (current_size > pid->fifo_capacity * 95 / 100 && !is_pusi) {
                    force_drop = 1;
                    pid->tel_audio_shed_count++;
                }
            } else {
                /* Data/Subtitle: standard drop */
                force_drop = 1;
            }
        }

        if (!force_drop && pid->fifo_capacity - tss_fifo_size(pid->ts_fifo) >= TSS_TS_PACKET_SIZE) {
            tss_fifo_write(pid->ts_fifo, packet, TSS_TS_PACKET_SIZE);
            pid->fifo_accept_bytes += TSS_TS_PACKET_SIZE;
        } else {
            pid->drop_bytes_total += TSS_TS_PACKET_SIZE;
            pid->cc_drop_count++;
            if (pid->type == TSS_PID_VIDEO || pid->type == TSS_PID_AUDIO)
                pid->needs_discontinuity = 1;

            if (pid->type == TSS_PID_PSI) {
                /* Section-aware rotation: Only rotate if incoming is a new section start */
                if (is_pusi) {
                    pid->tel_psi_rotate_count++;
                    tss_fifo_drain(pid->ts_fifo, TSS_TS_PACKET_SIZE);
                    while (tss_fifo_size(pid->ts_fifo) >= TSS_TS_PACKET_SIZE) {
                        uint8_t peek_pkt[TSS_TS_PACKET_SIZE];
                        tss_fifo_peek(pid->ts_fifo, peek_pkt, TSS_TS_PACKET_SIZE, 0);
                        if ((peek_pkt[1] & 0x40) != 0) {
                            break;
                        }
                        tss_fifo_drain(pid->ts_fifo, TSS_TS_PACKET_SIZE);
                    }
                    tss_fifo_write(pid->ts_fifo, packet, TSS_TS_PACKET_SIZE);
                }
            }
        }
    }
}
