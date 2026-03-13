#include "tsa_webhook.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_WEBHOOK_QUEUE 1024
#define MAX_RETRIES 3
#define SUPPRESSION_CACHE_SIZE 256
#define COOLDOWN_NS 10000000000ULL /* 10 seconds */

typedef struct {
    uint32_t hash;
    uint64_t last_sent_ns;
    uint32_t count;
    char last_stream_id[64];
    char last_event_type[32];
    char last_message[256];
    char last_level[16];
} suppression_entry_t;

typedef struct {
    char* json;
} webhook_msg_t;

struct tsa_webhook_engine {
    char url[1024];
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    webhook_msg_t queue[MAX_WEBHOOK_QUEUE];
    int head;
    int tail;
    int count;
    volatile bool running;

    suppression_entry_t cache[SUPPRESSION_CACHE_SIZE];
};

static uint32_t simple_hash(const char* s1, const char* s2) {
    uint32_t h = 0;
    if (s1)
        while (*s1) h = h * 31 + (uint32_t)(*s1++);
    if (s2)
        while (*s2) h = h * 31 + (uint32_t)(*s2++);
    return h;
}

static void send_now_locked(tsa_webhook_engine_t* eng, const char* stream_id, const char* event_type,
                            const char* message, const char* level, uint32_t count) {
    char buf[2048];
    uint64_t ts = (uint64_t)time(NULL);
    const char* sid = stream_id ? stream_id : "unknown";
    const char* lvl = level ? level : "CRITICAL";

    if (count > 1) {
        snprintf(buf, sizeof(buf),
                 "{\"stream_id\":\"%s\",\"alert_id\":\"%s\",\"level\":\"%s\",\"message\":\"%s (occurred %u "
                 "times)\",\"timestamp\":%llu,\"summary\":true}",
                 sid, event_type, lvl, message, count, (unsigned long long)ts);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"stream_id\":\"%s\",\"alert_id\":\"%s\",\"level\":\"%s\",\"message\":\"%s\",\"timestamp\":%llu}",
                 sid, event_type, lvl, message, (unsigned long long)ts);
    }

    if (eng->count < MAX_WEBHOOK_QUEUE) {
        eng->queue[eng->head].json = strdup(buf);
        eng->head = (eng->head + 1) % MAX_WEBHOOK_QUEUE;
        eng->count++;
        pthread_cond_signal(&eng->cond);
    }
}

static void* webhook_worker(void* arg) {
    tsa_webhook_engine_t* eng = (tsa_webhook_engine_t*)arg;
    CURL* curl = curl_easy_init();
    struct curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");

    while (eng->running) {
        char* msg_to_send = NULL;

        pthread_mutex_lock(&eng->lock);

        /* 1. Check for expired suppression entries that need a final summary flush */
        struct timespec ts_now;
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        uint64_t now_ns = (uint64_t)ts_now.tv_sec * 1000000000ULL + ts_now.tv_nsec;

        for (int i = 0; i < SUPPRESSION_CACHE_SIZE; i++) {
            if (eng->cache[i].count > 1 && (now_ns - eng->cache[i].last_sent_ns >= COOLDOWN_NS)) {
                send_now_locked(eng, eng->cache[i].last_stream_id, eng->cache[i].last_event_type,
                                eng->cache[i].last_message, eng->cache[i].last_level, eng->cache[i].count);
                eng->cache[i].count = 0;  // Reset
            }
        }

        /* 2. Wait for work */
        if (eng->count == 0 && eng->running) {
            struct timespec wait_time;
            clock_gettime(CLOCK_REALTIME, &wait_time);
            wait_time.tv_sec += 1;  // Wake up every second to check for flushes
            pthread_cond_timedwait(&eng->cond, &eng->lock, &wait_time);
        }

        if (eng->count > 0) {
            msg_to_send = eng->queue[eng->tail].json;
            eng->tail = (eng->tail + 1) % MAX_WEBHOOK_QUEUE;
            eng->count--;
        }
        pthread_mutex_unlock(&eng->lock);

        if (msg_to_send) {
            curl_easy_setopt(curl, CURLOPT_URL, eng->url);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, msg_to_send);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            if (curl_easy_perform(curl) != CURLE_OK) {
                // Log failure, but do not block worker with sleep. Fail fast so queue doesn't overflow.
                fprintf(stderr, "Webhook send failed, dropping message to prevent queue head-of-line blocking.\\n");
            }
            free(msg_to_send);
        }
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return NULL;
}

tsa_webhook_engine_t* tsa_webhook_init(const char* url) {
    tsa_webhook_engine_t* eng = calloc(1, sizeof(tsa_webhook_engine_t));
    strncpy(eng->url, url, sizeof(eng->url) - 1);
    eng->running = true;
    pthread_mutex_init(&eng->lock, NULL);
    pthread_cond_init(&eng->cond, NULL);
    pthread_create(&eng->thread, NULL, webhook_worker, eng);
    return eng;
}

void tsa_webhook_destroy(tsa_webhook_engine_t* eng) {
    if (!eng) return;
    eng->running = false;
    pthread_mutex_lock(&eng->lock);
    pthread_cond_broadcast(&eng->cond);
    pthread_mutex_unlock(&eng->lock);
    pthread_join(eng->thread, NULL);
    for (int i = 0; i < eng->count; i++) free(eng->queue[(eng->tail + i) % MAX_WEBHOOK_QUEUE].json);
    pthread_mutex_destroy(&eng->lock);
    pthread_cond_destroy(&eng->cond);
    free(eng);
}

void tsa_webhook_push_event(tsa_webhook_engine_t* eng, const char* stream_id, const char* event_type,
                            const char* message, const char* level) {
    if (!eng) return;
    uint32_t h = simple_hash(stream_id, event_type);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    pthread_mutex_lock(&eng->lock);
    for (int i = 0; i < SUPPRESSION_CACHE_SIZE; i++) {
        if (eng->cache[i].hash == h && eng->cache[i].count > 0) {
            if (now - eng->cache[i].last_sent_ns < COOLDOWN_NS) {
                eng->cache[i].count++;
                pthread_mutex_unlock(&eng->lock);
                return;
            }
        }
    }

    // New or fresh window
    int idx = -1;
    for (int i = 0; i < SUPPRESSION_CACHE_SIZE; i++)
        if (eng->cache[i].count == 0) {
            idx = i;
            break;
        }
    if (idx == -1) idx = (int)(now % SUPPRESSION_CACHE_SIZE);  // Random replacement

    eng->cache[idx].hash = h;
    eng->cache[idx].last_sent_ns = now;
    eng->cache[idx].count = 1;
    strncpy(eng->cache[idx].last_stream_id, stream_id ? stream_id : "unknown", 63);
    strncpy(eng->cache[idx].last_event_type, event_type, 31);
    strncpy(eng->cache[idx].last_message, message, 255);
    strncpy(eng->cache[idx].last_level, level ? level : "CRITICAL", 15);

    send_now_locked(eng, stream_id, event_type, message, level, 1);
    pthread_mutex_unlock(&eng->lock);
}

void tsa_webhook_push(tsa_webhook_engine_t* eng, const char* json_msg) {
    pthread_mutex_lock(&eng->lock);
    if (eng->count < MAX_WEBHOOK_QUEUE) {
        eng->queue[eng->head].json = strdup(json_msg);
        eng->head = (eng->head + 1) % MAX_WEBHOOK_QUEUE;
        eng->count++;
        pthread_cond_signal(&eng->cond);
    } else {
        fprintf(stderr, "[CRITICAL] Webhook queue overflow! Dropping alert: %s\\n", json_msg);
    }
    pthread_mutex_unlock(&eng->lock);
}
