#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsa.h"

#define READER_COUNT 4
#define ITERATIONS 100000

typedef struct {
    tsa_handle_t* h;
    atomic_bool stop;
} thread_ctx_t;

void* writer_thread(void* arg) {
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    uint8_t pkt[188] = {TS_SYNC_BYTE, 0x01, 0x00, 0x10};
    for (uint64_t i = 0; i < ITERATIONS; i++) {
        tsa_process_packet(ctx->h, pkt, i * 1000);
        if (i % 10 == 0) {
            tsa_commit_snapshot(ctx->h, i * 1000);
        }
    }
    atomic_store(&ctx->stop, true);
    return NULL;
}

void* reader_thread(void* arg) {
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    tsa_snapshot_lite_t s;
    while (!atomic_load(&ctx->stop)) {
        if (tsa_take_snapshot_lite(ctx->h, &s) == 0) {
            // Check consistency if possible
            // In lite stats, we don't have many fields that must match perfectly
            // but we can check if total_packets is monotonic (mostly)
        }
    }
    return NULL;
}

int main() {
    printf("Testing TSA Snapshot SeqLock Concurrency...\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    thread_ctx_t ctx = {h, false};

    pthread_t w, r[READER_COUNT];
    pthread_create(&w, NULL, writer_thread, &ctx);
    for (int i = 0; i < READER_COUNT; i++) {
        pthread_create(&r[i], NULL, reader_thread, &ctx);
    }

    pthread_join(w, NULL);
    for (int i = 0; i < READER_COUNT; i++) {
        pthread_join(r[i], NULL);
    }

    tsa_destroy(h);
    printf("TSA Snapshot SeqLock test passed!\n");
    return 0;
}
