#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "hal.h"
#include "internal.h"

/**
 * libtsshaper - Mock HAL Implementation (Virtual Time Domain)
 */

typedef struct {
    FILE* pcap_fp;
    uint64_t virtual_now_ns;
} mock_backend_t;

// GCC __thread for portable thread-local storage
static __thread mock_backend_t* current_mock_backend = NULL;

// Standard PCAP Global Header (Nanosecond magic: 0xa1b23c4d)
typedef struct {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    int32_t zone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} pcap_hdr_t;

typedef struct {
    uint32_t ts_sec;
    uint32_t ts_nsec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcaprec_hdr_t;

// Global or Thread-local to store the "last requested wait time"
static __thread uint64_t virtual_clock_ns = 1000000000ULL;

static int mock_io_init(tsshaper_t* ctx, void* params) {
    mock_backend_t* backend = calloc(1, sizeof(mock_backend_t));
    const char* filename = (const char*)params;

    backend->pcap_fp = fopen(filename, "wb");
    if (!backend->pcap_fp) {
        free(backend);
        return -1;
    }

    pcap_hdr_t hdr = {0xa1b23c4d, 2, 4, 0, 0, 65535, 1};
    fwrite(&hdr, 1, sizeof(hdr), backend->pcap_fp);

    backend->virtual_now_ns = 1000000000ULL;
    ctx->backend_priv = backend;
    current_mock_backend = backend;
    virtual_clock_ns = 1000000000ULL;
    return 0;
}

static int mock_io_send(tsshaper_t* ctx, struct mmsghdr* msgs, int count) {
    mock_backend_t* backend = (mock_backend_t*)ctx->backend_priv;
    if (!backend) return -1;

    for (int i = 0; i < count; i++) {
        uint8_t dummy_headers[42] = {0};
        dummy_headers[12] = 0x08;
        dummy_headers[13] = 0x00;
        dummy_headers[14] = 0x45;
        dummy_headers[16] = 0x00;
        dummy_headers[17] = 0xD8;
        dummy_headers[23] = 17;
        dummy_headers[24] = 0xAA;
        dummy_headers[25] = 0x27;
        dummy_headers[36] = 0x04;
        dummy_headers[37] = 0xD2;
        dummy_headers[38] = 0x00;
        dummy_headers[39] = 0xC4;

        // Use the current virtual clock for the PCAP timestamp
        pcaprec_hdr_t rec = {(uint32_t)(virtual_clock_ns / 1000000000ULL), (uint32_t)(virtual_clock_ns % 1000000000ULL),
                             42 + TS_PACKET_SIZE, 42 + TS_PACKET_SIZE};

        fwrite(&rec, 1, sizeof(rec), backend->pcap_fp);
        fwrite(dummy_headers, 1, 42, backend->pcap_fp);
        fwrite(msgs[i].msg_hdr.msg_iov[0].iov_base, 1, TS_PACKET_SIZE, backend->pcap_fp);

        // In virtual mode, the entire batch is emitted "at once" from the
        // perspective of the current clock. The Pacer will advance the clock
        // for the next individual packet in its own loop.
    }
    return count;
}

static void mock_io_close(tsshaper_t* ctx) {
    mock_backend_t* backend = (mock_backend_t*)ctx->backend_priv;
    if (backend) {
        if (backend->pcap_fp) fclose(backend->pcap_fp);
        free(backend);
        ctx->backend_priv = NULL;
        current_mock_backend = NULL;
    }
}

void hal_init_mock_backend(tsshaper_t* ctx) {
    ctx->hal_ops.io_init = mock_io_init;
    ctx->hal_ops.io_send = mock_io_send;
    ctx->hal_ops.io_close = mock_io_close;
}

// In virtual domain, hal_get_time_ns() returns the virtual clock which is
// advanced by hal_precision_wait() or io_send.
uint64_t hal_get_mock_time_ns(void) {
    return virtual_clock_ns;
}

// THIS IS THE KEY: virtual time ONLY advances when the pacer "waits" or "sends"
void hal_precision_wait_mock(uint64_t target_ns) {
    if (target_ns > virtual_clock_ns) {
        virtual_clock_ns = target_ns;
    }
}
