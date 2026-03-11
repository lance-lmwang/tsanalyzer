#include "tsa_auth.h"
#include "tsa_log.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define TAG "AUTH"
#define MAX_ID_TRACK 1024
#define BUCKET_CAPACITY 100.0
#define REFILL_RATE 10.0 // Tokens per second

typedef struct {
    char id[64];
    double tokens;
    uint64_t last_update_ns;
} rate_bucket_t;

static rate_bucket_t g_buckets[MAX_ID_TRACK];
static int g_bucket_count = 0;
static pthread_mutex_t g_auth_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_api_secret[64] = "tsanalyzer-default-secret";

static uint64_t get_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void tsa_auth_init(const char* secret) {
    pthread_mutex_lock(&g_auth_lock);
    if (secret) {
        strncpy(g_api_secret, secret, sizeof(g_api_secret) - 1);
    }
    pthread_mutex_unlock(&g_auth_lock);
    tsa_info(TAG, "API Security Engine Initialized (RateLimit: %.1f req/s)", REFILL_RATE);
}

bool tsa_auth_check_ratelimit(const char* client_id) {
    pthread_mutex_lock(&g_auth_lock);
    rate_bucket_t* b = NULL;
    for (int i = 0; i < g_bucket_count; i++) {
        if (strcmp(g_buckets[i].id, client_id) == 0) {
            b = &g_buckets[i];
            break;
        }
    }

    if (!b) {
        if (g_bucket_count < MAX_ID_TRACK) {
            b = &g_buckets[g_bucket_count++];
            strncpy(b->id, client_id, sizeof(b->id) - 1);
            b->tokens = BUCKET_CAPACITY;
            b->last_update_ns = get_ns();
        } else {
            pthread_mutex_unlock(&g_auth_lock);
            return false;
        }
    }

    uint64_t now = get_ns();
    double elapsed = (double)(now - b->last_update_ns) / 1e9;
    b->tokens += elapsed * REFILL_RATE;
    if (b->tokens > BUCKET_CAPACITY) b->tokens = BUCKET_CAPACITY;
    b->last_update_ns = now;

    if (b->tokens >= 1.0) {
        b->tokens -= 1.0;
        pthread_mutex_unlock(&g_auth_lock);
        return true;
    }

    pthread_mutex_unlock(&g_auth_lock);
    return false;
}

bool tsa_auth_verify_request(struct mg_http_message *hm) {
    struct mg_str *auth_header = mg_http_get_header(hm, "X-TSA-Token");
    if (!auth_header) return false;

    pthread_mutex_lock(&g_auth_lock);
    struct mg_str sec = mg_str(g_api_secret);
    int res = mg_strcmp(*auth_header, sec);
    pthread_mutex_unlock(&g_auth_lock);

    return (res == 0);
}
