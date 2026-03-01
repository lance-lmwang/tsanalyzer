#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static _Atomic uint64_t g_malloc_count = 0;
static bool g_tracking_enabled = false;
static __thread bool g_in_hook = false;

typedef void* (*malloc_t)(size_t);
typedef void (*free_t)(void*);
typedef void* (*calloc_t)(size_t, size_t);
typedef void* (*realloc_t)(void*, size_t);
typedef int (*posix_memalign_t)(void**, size_t, size_t);

static malloc_t real_malloc = NULL;
static free_t real_free = NULL;
static calloc_t real_calloc = NULL;
static realloc_t real_realloc = NULL;
static posix_memalign_t real_posix_memalign = NULL;

static void init_real_funcs() {
    if (g_in_hook) return;
    g_in_hook = true;
    if (!real_malloc) real_malloc = (malloc_t)dlsym(RTLD_NEXT, "malloc");
    if (!real_free) real_free = (free_t)dlsym(RTLD_NEXT, "free");
    if (!real_calloc) real_calloc = (calloc_t)dlsym(RTLD_NEXT, "calloc");
    if (!real_realloc) real_realloc = (realloc_t)dlsym(RTLD_NEXT, "realloc");
    if (!real_posix_memalign) real_posix_memalign = (posix_memalign_t)dlsym(RTLD_NEXT, "posix_memalign");
    g_in_hook = false;
}

// Temporary buffer for dlsym bootstrap
static char bootstrap_buffer[4096];
static size_t bootstrap_offset = 0;

void* malloc(size_t size) {
    if (!real_malloc) {
        if (g_in_hook) {
            // Primitive allocator for dlsym internal calls
            void* ptr = bootstrap_buffer + bootstrap_offset;
            bootstrap_offset += (size + 15) & ~15;
            return ptr;
        }
        init_real_funcs();
    }
    if (g_tracking_enabled && !g_in_hook) atomic_fetch_add(&g_malloc_count, 1);
    return real_malloc(size);
}

void free(void* ptr) {
    if (ptr >= (void*)bootstrap_buffer && ptr < (void*)(bootstrap_buffer + sizeof(bootstrap_buffer))) return;
    if (!real_free) init_real_funcs();
    real_free(ptr);
}

void* calloc(size_t nmemb, size_t size) {
    if (!real_calloc) {
        void* ptr = malloc(nmemb * size);
        if (ptr) {
            for (size_t i = 0; i < nmemb * size; i++) ((char*)ptr)[i] = 0;
        }
        return ptr;
    }
    if (g_tracking_enabled && !g_in_hook) atomic_fetch_add(&g_malloc_count, 1);
    return real_calloc(nmemb, size);
}

void* realloc(void* ptr, size_t size) {
    if (!real_realloc) init_real_funcs();
    if (g_tracking_enabled && ptr == NULL && !g_in_hook) atomic_fetch_add(&g_malloc_count, 1);
    return real_realloc(ptr, size);
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    if (!real_posix_memalign) init_real_funcs();
    if (g_tracking_enabled && !g_in_hook) atomic_fetch_add(&g_malloc_count, 1);
    return real_posix_memalign(memptr, alignment, size);
}

void start_allocation_tracking() {
    g_tracking_enabled = true;
}
void stop_allocation_tracking() {
    g_tracking_enabled = false;
}
uint64_t get_malloc_count() {
    return atomic_load(&g_malloc_count);
}
void reset_malloc_count() {
    atomic_store(&g_malloc_count, 0);
}
