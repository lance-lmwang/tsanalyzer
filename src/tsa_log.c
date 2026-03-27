#define _GNU_SOURCE
#include "tsa_log.h"

#include <pthread.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define TSA_LOG_MSG_MAX 256
#define TSA_LOG_TAG_MAX 16
#define LOCAL_RING_SIZE 1024
#define MAX_THREADS 256

typedef struct {
    uint64_t ts_ns;
    uint64_t context_id;
    uint32_t pid;
    uint32_t tid;
    uint16_t level;
    char tag[TSA_LOG_TAG_MAX];
    const char* file;
    uint32_t line;
    char msg[TSA_LOG_MSG_MAX];
} tsa_log_entry_t;

typedef struct {
    alignas(64) _Atomic uint32_t head; /* Updated by Producer */
    alignas(64) _Atomic uint32_t tail; /* Updated by Worker */
    tsa_log_entry_t entries[LOCAL_RING_SIZE];
    uint32_t thread_id;
} tsa_ringbuffer_t;

typedef struct {
    tsa_ringbuffer_t* rb;
    _Atomic bool active;
} tsa_log_slot_t;

static struct {
    tsa_log_slot_t slots[MAX_THREADS];
    _Atomic uint32_t slot_count;
    pthread_mutex_t registry_lock;

    pthread_t worker;
    _Atomic bool active;
    tsa_log_level_t level;
    bool json;

    FILE* file_ptr;
    _Atomic uint64_t drop_count;
} g_log_sys = {.slot_count = 0,
               .registry_lock = PTHREAD_MUTEX_INITIALIZER,
               .active = false,
               .level = TSA_LOG_INFO,
               .json = false,
               .file_ptr = NULL,
               .drop_count = 0};

static _Thread_local tsa_ringbuffer_t* t_local_ring = NULL;

const char* level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "REPORT"};

void tsa_log_result(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    va_end(args);
}

static uint64_t get_nanos() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void render_entry(tsa_log_entry_t* e) {
    time_t sec = e->ts_ns / 1000000000ULL;
    struct tm t;
    localtime_r(&sec, &t);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &t);

    if (g_log_sys.json) {
        fprintf(stdout,
                "{\"ts\":%llu,\"level\":\"%s\",\"tag\":\"%s\",\"tid\":%u,\"pid\":%u,\"ctx\":%llu,\"file\":\"%s\","
                "\"line\":%u,\"msg\":\"%s\"}\n",
                (unsigned long long)e->ts_ns, level_names[e->level], e->tag, e->tid, e->pid,
                (unsigned long long)e->context_id, e->file, e->line, e->msg);
    } else {
        fprintf(stdout, "[%s.%03llu][%s][%s][%s:%u] %s\n", time_str,
                (unsigned long long)((e->ts_ns % 1000000000ULL) / 1000000), level_names[e->level], e->tag, e->file,
                e->line, e->msg);
    }
}

static void* log_worker_thread(void* arg) {
    (void)arg;
    while (atomic_load_explicit(&g_log_sys.active, memory_order_relaxed) ||
           atomic_load_explicit(&g_log_sys.slot_count, memory_order_acquire) > 0) {
        bool work_done = false;
        uint32_t count = atomic_load_explicit(&g_log_sys.slot_count, memory_order_acquire);

        for (uint32_t i = 0; i < count; i++) {
            tsa_ringbuffer_t* rb = g_log_sys.slots[i].rb;
            if (!rb) continue;

            /* Batch drain: process all entries currently available in this ring */
            uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
            uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);

            while (tail != head) {
                tsa_log_entry_t* e = &rb->entries[tail % LOCAL_RING_SIZE];
                render_entry(e);
                tail++;
                atomic_store_explicit(&rb->tail, tail, memory_order_release);
                work_done = true;
            }

            /* If thread is no longer active and ring is empty, we could potentially reclaim the slot.
             * For now, we just keep polling till shutdown. */
        }

        if (!work_done) {
            if (!atomic_load_explicit(&g_log_sys.active, memory_order_relaxed)) break;
            usleep(10000); /* 10ms idle sleep */
        }
    }
    return NULL;
}

static void register_current_thread() {
    tsa_ringbuffer_t* rb = calloc(1, sizeof(tsa_ringbuffer_t));
    rb->thread_id = (uint32_t)syscall(SYS_gettid);
    atomic_init(&rb->head, 0);
    atomic_init(&rb->tail, 0);

    pthread_mutex_lock(&g_log_sys.registry_lock);
    uint32_t idx = atomic_load_explicit(&g_log_sys.slot_count, memory_order_relaxed);
    if (idx < MAX_THREADS) {
        g_log_sys.slots[idx].rb = rb;
        atomic_store_explicit(&g_log_sys.slots[idx].active, true, memory_order_release);
        atomic_store_explicit(&g_log_sys.slot_count, idx + 1, memory_order_release);
        t_local_ring = rb;
    } else {
        free(rb);
    }
    pthread_mutex_unlock(&g_log_sys.registry_lock);
}

#include "mongoose.h"

/* Helper to bridge Mongoose logs to TSA log system */
static void mg_log_bridge(char c, void* param) {
    (void)param;
    static char buf[512];
    static int idx = 0;
    if (c == '\n' || idx >= 511) {
        buf[idx] = '\0';
        if (idx > 0) tsa_debug("MONGOOSE", "%s", buf);
        idx = 0;
    } else {
        buf[idx++] = c;
    }
}

void tsa_log_init(const char* file_path) {
    if (atomic_load_explicit(&g_log_sys.active, memory_order_relaxed)) return;

    /* Redirect Mongoose logs to our bridge */
    mg_log_set_fn(mg_log_bridge, NULL);
    mg_log_set(MG_LL_ERROR);

    if (file_path) {
        g_log_sys.file_ptr = fopen(file_path, "a");
    }

    atomic_store_explicit(&g_log_sys.active, true, memory_order_release);
    pthread_create(&g_log_sys.worker, NULL, log_worker_thread, NULL);
}

void tsa_log_destroy(void) {
    atomic_store_explicit(&g_log_sys.active, false, memory_order_release);
    pthread_join(g_log_sys.worker, NULL);

    if (g_log_sys.file_ptr) {
        fclose(g_log_sys.file_ptr);
        g_log_sys.file_ptr = NULL;
    }

    pthread_mutex_lock(&g_log_sys.registry_lock);
    uint32_t count = atomic_load_explicit(&g_log_sys.slot_count, memory_order_relaxed);
    for (uint32_t i = 0; i < count; i++) {
        free(g_log_sys.slots[i].rb);
        g_log_sys.slots[i].rb = NULL;
        atomic_store(&g_log_sys.slots[i].active, false);
    }
    atomic_store(&g_log_sys.slot_count, 0);
    pthread_mutex_unlock(&g_log_sys.registry_lock);
}

void tsa_log_set_level(tsa_log_level_t level) {
    g_log_sys.level = level;
}

void tsa_log_set_json(bool enabled) {
    g_log_sys.json = enabled;
}

void tsa_log_impl(tsa_log_level_t level, const char* file, int line, const char* tag, uint64_t ctx_id, uint32_t pid,
                  const char* fmt, ...) {
    if (level < g_log_sys.level && level != TSA_LOG_REPORT) return;
    if (!atomic_load_explicit(&g_log_sys.active, memory_order_relaxed)) {
        /* Fallback for pre-init or post-destroy logging */
        va_list args;
        va_start(args, fmt);
        fprintf(stderr, "[%s][%s] ", tag ? tag : "sys", level_names[level]);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
        return;
    }

    if (!t_local_ring) register_current_thread();
    if (!t_local_ring) return;

    uint32_t head = atomic_load_explicit(&t_local_ring->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&t_local_ring->tail, memory_order_acquire);

    if (head - tail >= LOCAL_RING_SIZE) {
        atomic_fetch_add_explicit(&g_log_sys.drop_count, 1, memory_order_relaxed);
        return;
    }

    tsa_log_entry_t* e = &t_local_ring->entries[head % LOCAL_RING_SIZE];
    e->ts_ns = get_nanos();
    e->context_id = ctx_id;
    e->pid = pid;
    e->tid = t_local_ring->thread_id;
    e->level = level;
    e->line = line;

    const char* last_slash = strrchr(file, '/');
    e->file = last_slash ? last_slash + 1 : file;

    if (tag) {
        strncpy(e->tag, tag, TSA_LOG_TAG_MAX - 1);
        e->tag[TSA_LOG_TAG_MAX - 1] = '\0';
    } else {
        strcpy(e->tag, "sys");
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(e->msg, TSA_LOG_MSG_MAX - 1, fmt, args);
    if (written >= TSA_LOG_MSG_MAX - 1) {
        /* Mark truncation */
        const char* trunc_msg = " [TRUNCATED]";
        size_t trunc_len = strlen(trunc_msg);
        memcpy(e->msg + TSA_LOG_MSG_MAX - 1 - trunc_len, trunc_msg, trunc_len);
    }
    e->msg[TSA_LOG_MSG_MAX - 1] = '\0';
    va_end(args);

    atomic_store_explicit(&t_local_ring->head, head + 1, memory_order_release);
}
