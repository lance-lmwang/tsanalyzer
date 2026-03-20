#define _GNU_SOURCE
#include "internal.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <string.h>

static void restamp_pcr(uint8_t* data, uint64_t pcr_27mhz) {
    uint64_t base = pcr_27mhz / 300;
    uint16_t ext = pcr_27mhz % 300;

    // PCR is at offset 6 in TS packet if AF exists and PCR flag is set
    // This function assumes we already verified the PCR location
    data[6] = (base >> 25) & 0xFF;
    data[7] = (base >> 17) & 0xFF;
    data[8] = (base >> 9) & 0xFF;
    data[9] = (base >> 1) & 0xFF;
    data[10] = ((base & 1) << 7) | 0x7E | ((ext >> 8) & 1);
    data[11] = ext & 0xFF;
}

void* pacer_thread_func(void* arg) {
    tsa_shaper_t* ctx = (tsa_shaper_t*)arg;
    hal_setup_rt(ctx->cpu_affinity, ctx->sched_priority);

    ctx->start_time_ns = hal_get_time_ns();
    ctx->next_packet_time_ns = ctx->start_time_ns;

    uint64_t statmux_last_ns = ctx->start_time_ns;

    // Batching for sendmmsg
    #define BATCH_SIZE 7
    struct mmsghdr msgs[BATCH_SIZE];
    struct iovec iovecs[BATCH_SIZE];
    uint8_t buffer[BATCH_SIZE][TS_PACKET_SIZE];

    memset(msgs, 0, sizeof(msgs));
    for (int i = 0; i < BATCH_SIZE; i++) {
        iovecs[i].iov_base = buffer[i];
        iovecs[i].iov_len = TS_PACKET_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        if (ctx->output_mode == TSA_OUT_UDP || ctx->output_mode == TSA_OUT_RTP) {
            msgs[i].msg_hdr.msg_name = &ctx->output_addr;
            msgs[i].msg_hdr.msg_namelen = sizeof(ctx->output_addr);
        }
    }

    int batch_idx = 0;

    while (ctx->running) {
        uint64_t now = hal_get_time_ns();

        // 1. Periodic StatMux rebalance
        if (now - statmux_last_ns > 100000000) { // 100ms
            statmux_rebalance(ctx);
            statmux_last_ns = now;
        }

        // 2. Select packet
        ts_packet_t* pkt = interleaver_select(ctx);
        memcpy(buffer[batch_idx], pkt->data, TS_PACKET_SIZE);

        // 3. PCR Restamping
        bool has_af = (buffer[batch_idx][3] & 0x20) != 0;
        if (has_af && buffer[batch_idx][4] > 0) {
            bool has_pcr = (buffer[batch_idx][5] & 0x10) != 0;
            if (has_pcr) {
                // Calculate PCR based on scheduled transmission time
                uint64_t pcr_val = (ctx->next_packet_time_ns - ctx->start_time_ns) * TS_SYSTEM_CLOCK_HZ / NS_PER_SEC;
                restamp_pcr(buffer[batch_idx], pcr_val);

                // Track jitter stats (simulated vs target)
                double jitter = (double)now - (double)ctx->next_packet_time_ns;
                atomic_fetch_add((_Atomic uint64_t*)&ctx->pcr_jitter_ns_sum, (uint64_t)(jitter > 0 ? jitter : -jitter));
                atomic_fetch_add(&ctx->pcr_count, 1);
            }
        }

        batch_idx++;

        // 4. Pacing and Sending
        if (batch_idx == BATCH_SIZE) {
            // Wait for the scheduled time of the FIRST packet in the batch
            // In a more complex pacer, we might want to space them out even within a batch
            // but for UDP/RTP batching, they are sent as a single unit.
            hal_precision_wait(ctx->next_packet_time_ns - (BATCH_SIZE - 1) * ctx->packet_interval_ns);

            if (ctx->output_fd >= 0) {
                if (ctx->output_mode == TSA_OUT_UDP) {
                    // Send 7 packets in ONE UDP datagram
                    struct iovec iov_batch;
                    iov_batch.iov_base = buffer;
                    iov_batch.iov_len = BATCH_SIZE * TS_PACKET_SIZE;
                    struct msghdr msg;
                    memset(&msg, 0, sizeof(msg));
                    msg.msg_iov = &iov_batch;
                    msg.msg_iovlen = 1;
                    msg.msg_name = &ctx->output_addr;
                    msg.msg_namelen = sizeof(ctx->output_addr);
                    sendmsg(ctx->output_fd, &msg, 0);
                } else {
                    // Default to individual packets via sendmmsg
                    sendmmsg(ctx->output_fd, msgs, BATCH_SIZE, 0);
                }
            }

            atomic_fetch_add(&ctx->bytes_sent, BATCH_SIZE * TS_PACKET_SIZE);
            batch_idx = 0;
        }

        ctx->next_packet_time_ns += ctx->packet_interval_ns;

        // Catch-up logic if we fall behind
        if (hal_get_time_ns() > ctx->next_packet_time_ns + 10000000) { // 10ms
            ctx->next_packet_time_ns = hal_get_time_ns();
        }
    }

    return NULL;
}
