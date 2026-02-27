#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>

static _Atomic uint64_t g_malloc_count = 0;
static bool g_tracking_enabled = false;

typedef void* (*malloc_t)(size_t);
typedef void (*free_t)(void*);

static malloc_t real_malloc = NULL;
static free_t real_free = NULL;

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

void* malloc(size_t size) {
    if (!real_malloc) {
        real_malloc = (malloc_t)dlsym(RTLD_NEXT, "malloc");
    }
    
    if (g_tracking_enabled) {
        atomic_fetch_add(&g_malloc_count, 1);
    }
    
    return real_malloc(size);
}

void free(void* ptr) {
    if (!real_free) {
        real_free = (free_t)dlsym(RTLD_NEXT, "free");
    }
    
    // We don't necessarily count frees for zero-malloc audit, 
    // but we must call the real free.
    real_free(ptr);
}
