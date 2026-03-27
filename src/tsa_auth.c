#include "tsa_auth.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tsa_log.h"

#define TAG "AUTH"
#define MAX_ID_TRACK 1024
#define BUCKET_CAPACITY 100.0
#define REFILL_RATE 10.0

typedef struct {
    char id[64];
    double tokens;
    uint64_t last_update_ns;
} rate_bucket_t;

static rate_bucket_t g_buckets[MAX_ID_TRACK];
static int g_bucket_count = 0;
static pthread_mutex_t g_auth_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_api_secret[64] = "tsanalyzer-default-secret";
static bool g_auth_enabled = true;

static uint64_t get_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int b64url_decode(const char* in, int in_len, unsigned char* out) {
    BIO *b64, *bmem;
    char* temp = malloc(in_len + 5);
    int i, len;
    for (i = 0; i < in_len; i++) {
        if (in[i] == '-')
            temp[i] = '+';
        else if (in[i] == '_')
            temp[i] = '/';
        else
            temp[i] = in[i];
    }
    int pad = (4 - (in_len % 4)) % 4;
    for (i = 0; i < pad; i++) temp[in_len + i] = '=';
    len = in_len + pad;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new_mem_buf(temp, len);
    bmem = BIO_push(b64, bmem);
    int decoded_len = BIO_read(bmem, out, len);
    BIO_free_all(bmem);
    free(temp);
    return decoded_len;
}

void tsa_auth_set_enabled(bool enabled) {
    pthread_mutex_lock(&g_auth_lock);
    g_auth_enabled = enabled;
    pthread_mutex_unlock(&g_auth_lock);
}

void tsa_auth_init(const char* secret) {
    pthread_mutex_lock(&g_auth_lock);
    const char* env_secret = getenv("TSA_API_SECRET");
    if (env_secret) {
        strncpy(g_api_secret, env_secret, sizeof(g_api_secret) - 1);
        tsa_info(TAG, "API Secret loaded from environment");
    } else if (secret) {
        strncpy(g_api_secret, secret, sizeof(g_api_secret) - 1);
    }
    pthread_mutex_unlock(&g_auth_lock);
    tsa_info(TAG, "API Security Engine Initialized (RateLimit: %.1f req/s)", REFILL_RATE);
}

bool tsa_auth_verify_request(struct mg_http_message* hm) {
    if (!g_auth_enabled) return true;
    struct mg_str* auth = mg_http_get_header(hm, "Authorization");
    struct mg_str token_str = mg_str("");

    if (auth && auth->len > 7 && strncasecmp(auth->buf, "Bearer ", 7) == 0) {
        token_str = mg_str_n(auth->buf + 7, auth->len - 7);
    } else {
        return false;
    }

    char* token = malloc(token_str.len + 1);
    memcpy(token, token_str.buf, token_str.len);
    token[token_str.len] = '\0';

    char* dot1 = strchr(token, '.');
    char* dot2 = dot1 ? strchr(dot1 + 1, '.') : NULL;
    if (!dot1 || !dot2) {
        free(token);
        return false;
    }

    int sig_start = (int)(dot2 - token) + 1;
    int data_len = (int)(dot2 - token);
    int sig_b64_len = (int)token_str.len - sig_start;

    unsigned char sig[64];
    int sig_len = b64url_decode(token + sig_start, sig_b64_len, sig);

    unsigned char expected_sig[EVP_MAX_MD_SIZE];
    unsigned int expected_sig_len;

    pthread_mutex_lock(&g_auth_lock);
    HMAC(EVP_sha256(), g_api_secret, strlen(g_api_secret), (unsigned char*)token, data_len, expected_sig,
         &expected_sig_len);
    pthread_mutex_unlock(&g_auth_lock);

    bool match = (sig_len == (int)expected_sig_len) && (CRYPTO_memcmp(sig, expected_sig, sig_len) == 0);
    free(token);
    return match;
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
