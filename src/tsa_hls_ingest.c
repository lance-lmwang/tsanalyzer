#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <curl/curl.h>
#include "tsa_hls_parser.h"
#include "tsa_source.h"

#define MAX_QUEUE_SIZE 10

typedef struct {
    char url[MAX_URL_LEN];
    uint64_t sequence;
} download_task_t;

typedef struct {
    tsa_source_callbacks_t cbs;
    void *user_data;
    char m3u8_url[MAX_URL_LEN];
    uint64_t last_seq;
    volatile bool running;
    pthread_t thread;
    CURL *curl;
} hls_ingest_t;

typedef struct {
    char *data;
    size_t size;
} memory_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    memory_t *mem = (memory_t *)userp;
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

static void download_segment(hls_ingest_t *ingest, const char *url) {
    CURL *curl = ingest->curl;
    memory_t chunk = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK && chunk.size > 0) {
        int count = chunk.size / 188;
        if (count > 0) {
            // Push to analyzer in bulk
            ingest->cbs.on_packets(ingest->user_data, (uint8_t*)chunk.data, count, 0);
        }
    }
    free(chunk.data);
}

static void on_segment_found(void *user_data, const tsa_hls_segment_t *seg) {
    hls_ingest_t *ingest = (hls_ingest_t *)user_data;
    if (seg->sequence > ingest->last_seq) {
        download_segment(ingest, seg->url);
        ingest->last_seq = seg->sequence;
    }
}

static void* hls_worker(void* arg) {
    hls_ingest_t *ingest = (hls_ingest_t *)arg;
    ingest->curl = curl_easy_init();
    curl_easy_setopt(ingest->curl, CURLOPT_USERAGENT, "TsAnalyzer-HLS/1.0");
    curl_easy_setopt(ingest->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(ingest->curl, CURLOPT_TIMEOUT, 10L);

    while (ingest->running) {
        memory_t chunk = {0};
        curl_easy_setopt(ingest->curl, CURLOPT_URL, ingest->m3u8_url);
        curl_easy_setopt(ingest->curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(ingest->curl, CURLOPT_WRITEDATA, (void *)&chunk);

        CURLcode res = curl_easy_perform(ingest->curl);
        if (res == CURLE_OK) {
            tsa_hls_context_t ctx = {0};
            strncpy(ctx.master_url, ingest->m3u8_url, sizeof(ctx.master_url)-1);
            ctx.on_segment = on_segment_found;
            ctx.user_data = ingest;

            tsa_hls_parse_m3u8(&ctx, chunk.data, chunk.size);

            uint64_t sleep_ms = ctx.target_duration ? (ctx.target_duration * 500) : 2000;
            if (sleep_ms < 1000) sleep_ms = 1000;
            usleep(sleep_ms * 1000);
        } else {
            usleep(2000000);
        }
        free(chunk.data);
    }

    curl_easy_cleanup(ingest->curl);
    return NULL;
}

void* tsa_hls_ingest_start(const char *url, const tsa_source_callbacks_t *cbs, void *user_data) {
    hls_ingest_t *ingest = calloc(1, sizeof(hls_ingest_t));
    ingest->cbs = *cbs;
    ingest->user_data = user_data;
    ingest->running = true;
    strncpy(ingest->m3u8_url, url, sizeof(ingest->m3u8_url)-1);

    pthread_create(&ingest->thread, NULL, hls_worker, ingest);
    return ingest;
}

void tsa_hls_ingest_stop(void *handle) {
    hls_ingest_t *ingest = (hls_ingest_t *)handle;
    if (!ingest) return;
    ingest->running = false;
    pthread_join(ingest->thread, NULL);
    free(ingest);
}
