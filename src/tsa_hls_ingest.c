#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "tsa_hls_parser.h"
#include "tsa_source.h"

#define MAX_QUEUE_SIZE 32

typedef struct {
    char url[MAX_URL_LEN];
    uint64_t sequence;
    double duration;
} download_task_t;

typedef struct {
    tsa_source_callbacks_t cbs;
    void *user_data;
    char m3u8_url[MAX_URL_LEN];
    uint64_t last_seq;
    volatile bool running;

    pthread_t thread_poll;
    pthread_t thread_dl;

    download_task_t queue[MAX_QUEUE_SIZE];
    int q_head;
    int q_tail;
    int q_size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    uint64_t total_errors;
    double data_duration_s;
    long long sys_clock_us;

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

static long long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void update_hls_metrics(hls_ingest_t *ingest, double seg_duration) {
    pthread_mutex_lock(&ingest->mutex);
    long long now_us = get_time_us();
    if (ingest->sys_clock_us <= 0) {
        ingest->sys_clock_us = now_us;
        ingest->data_duration_s = seg_duration;
    } else {
        double elapsed_s = (double)(now_us - ingest->sys_clock_us) / 1000000.0;
        double rst = ingest->data_duration_s - elapsed_s;
        if (rst < 0) {
            ingest->sys_clock_us = now_us;
            ingest->data_duration_s = seg_duration;
            rst = seg_duration;
        } else {
            ingest->data_duration_s += seg_duration;
            rst += seg_duration;
        }

        if (ingest->cbs.on_hls_stats) {
            ingest->cbs.on_hls_stats(ingest->user_data, rst * 1000.0, ingest->total_errors);
        }
    }
    pthread_mutex_unlock(&ingest->mutex);
}

static void *hls_downloader(void *arg) {
    hls_ingest_t *ingest = (hls_ingest_t *)arg;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TsAnalyzer-HLS/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    while (ingest->running) {
        download_task_t task;

        pthread_mutex_lock(&ingest->mutex);
        while (ingest->q_size == 0 && ingest->running) {
            pthread_cond_wait(&ingest->cond, &ingest->mutex);
        }
        if (!ingest->running) {
            pthread_mutex_unlock(&ingest->mutex);
            break;
        }
        task = ingest->queue[ingest->q_head];
        ingest->q_head = (ingest->q_head + 1) % MAX_QUEUE_SIZE;
        ingest->q_size--;
        pthread_mutex_unlock(&ingest->mutex);

        int retries = 3;
        bool success = false;

        while (retries > 0 && ingest->running) {
            memory_t chunk = {0};
            curl_easy_setopt(curl, CURLOPT_URL, task.url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK && chunk.size > 0) {
                int count = chunk.size / 188;
                if (count > 0 && ingest->cbs.on_packets) {
                    ingest->cbs.on_packets(ingest->user_data, (uint8_t *)chunk.data, count, 0);
                }
                update_hls_metrics(ingest, task.duration);
                success = true;
                free(chunk.data);
                break;
            }

            free(chunk.data);
            retries--;

            pthread_mutex_lock(&ingest->mutex);
            ingest->total_errors++;
            pthread_mutex_unlock(&ingest->mutex);

            if (retries > 0 && ingest->running) {
                usleep((3 - retries) * 500000);  // Exponential-ish backoff
            }
        }

        if (!success && ingest->cbs.on_status) {
            ingest->cbs.on_status(ingest->user_data, -1, "Segment download failed after retries");
        }
    }

    curl_easy_cleanup(curl);
    return NULL;
}

static void on_segment_found(void *user_data, const tsa_hls_segment_t *seg) {
    hls_ingest_t *ingest = (hls_ingest_t *)user_data;
    if (seg->sequence > ingest->last_seq) {
        pthread_mutex_lock(&ingest->mutex);
        if (ingest->q_size < MAX_QUEUE_SIZE) {
            ingest->queue[ingest->q_tail].sequence = seg->sequence;
            ingest->queue[ingest->q_tail].duration = seg->duration;
            snprintf(ingest->queue[ingest->q_tail].url, MAX_URL_LEN, "%s", seg->url);
            ingest->q_tail = (ingest->q_tail + 1) % MAX_QUEUE_SIZE;
            ingest->q_size++;
            ingest->last_seq = seg->sequence;
            pthread_cond_signal(&ingest->cond);
        }
        pthread_mutex_unlock(&ingest->mutex);
    }
}

static void *hls_worker(void *arg) {
    hls_ingest_t *ingest = (hls_ingest_t *)arg;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TsAnalyzer-HLS/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    while (ingest->running) {
        memory_t chunk = {0};
        curl_easy_setopt(curl, CURLOPT_URL, ingest->m3u8_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            tsa_hls_context_t ctx = {0};
            snprintf(ctx.master_url, sizeof(ctx.master_url), "%s", ingest->m3u8_url);
            ctx.on_segment = on_segment_found;
            ctx.user_data = ingest;

            tsa_hls_parse_m3u8(&ctx, chunk.data, chunk.size);

            uint64_t sleep_ms = ctx.target_duration ? (ctx.target_duration * 500) : 2000;
            if (sleep_ms < 1000) sleep_ms = 1000;

            // Allow quick exit during sleep
            int intervals = sleep_ms / 100;
            while (intervals-- > 0 && ingest->running) {
                usleep(100000);
            }
        } else {
            int intervals = 20;  // 2 seconds
            while (intervals-- > 0 && ingest->running) {
                usleep(100000);
            }
        }
        free(chunk.data);
    }

    curl_easy_cleanup(curl);
    return NULL;
}

void *tsa_hls_ingest_start(const char *url, const tsa_source_callbacks_t *cbs, void *user_data) {
    hls_ingest_t *ingest = calloc(1, sizeof(hls_ingest_t));
    ingest->cbs = *cbs;
    ingest->user_data = user_data;
    ingest->running = true;
    snprintf(ingest->m3u8_url, sizeof(ingest->m3u8_url), "%s", url);

    pthread_mutex_init(&ingest->mutex, NULL);
    pthread_cond_init(&ingest->cond, NULL);

    pthread_create(&ingest->thread_poll, NULL, hls_worker, ingest);
    pthread_create(&ingest->thread_dl, NULL, hls_downloader, ingest);

    return ingest;
}

void tsa_hls_ingest_stop(void *handle) {
    hls_ingest_t *ingest = (hls_ingest_t *)handle;
    if (!ingest) return;

    ingest->running = false;

    pthread_mutex_lock(&ingest->mutex);
    pthread_cond_signal(&ingest->cond);
    pthread_mutex_unlock(&ingest->mutex);

    pthread_join(ingest->thread_poll, NULL);
    pthread_join(ingest->thread_dl, NULL);

    pthread_mutex_destroy(&ingest->mutex);
    pthread_cond_destroy(&ingest->cond);

    free(ingest);
}
