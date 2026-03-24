#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "hal.h"
#include "internal.h"

typedef struct {
    tss_write_cb write_cb;
    void* opaque;
} callback_backend_t;

static int callback_io_init(tsshaper_t* ctx, void* params) {
    callback_backend_t* backend = calloc(1, sizeof(callback_backend_t));
    if (!backend) return -1;

    // params is expected to be a pointer to tsshaper_config_t or similar
    // But we already have the info in the main config.
    // For simplicity, we'll assume the caller (tsshaper_create)
    // will set these up after hal_init_ops.
    ctx->backend_priv = backend;
    return 0;
}

static int callback_io_send(tsshaper_t* ctx, struct mmsghdr* msgs, int count) {
    callback_backend_t* backend = (callback_backend_t*)ctx->backend_priv;
    if (!backend || !backend->write_cb) return -1;

    int sent = 0;
    for (int i = 0; i < count; i++) {
        // mmsghdr contains iovec which points to our 188-byte (or 192-byte padded) packets
        // We only send the 188 bytes of actual TS data to the callback
        uint8_t* pkt_data = (uint8_t*)msgs[i].msg_hdr.msg_iov[0].iov_base;
        if (backend->write_cb(pkt_data, backend->opaque) == 0) {
            sent++;
        } else {
            break; // Callback requested stop or error
        }
    }
    return sent;
}

static void callback_io_close(tsshaper_t* ctx) {
    if (ctx->backend_priv) {
        free(ctx->backend_priv);
        ctx->backend_priv = NULL;
    }
}

void hal_init_callback_backend(tsshaper_t* ctx, tss_write_cb cb, void* opaque) {
    ctx->hal_ops.io_init  = callback_io_init;
    ctx->hal_ops.io_send  = callback_io_send;
    ctx->hal_ops.io_close = callback_io_close;

    // We need to trigger io_init to allocate the private struct
    callback_io_init(ctx, NULL);

    callback_backend_t* backend = (callback_backend_t*)ctx->backend_priv;
    backend->write_cb = cb;
    backend->opaque = opaque;
}
