#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

// Define a test structure to use as a slab
typedef struct {
    uint32_t id;
    char data[64];
} test_slab_t;

void test_basic_allocation() {
    printf("Running test_basic_allocation...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    void* slab1 = tsa_mem_pool_alloc(h, sizeof(test_slab_t));
    assert(slab1 != NULL);

    void* slab2 = tsa_mem_pool_alloc(h, sizeof(test_slab_t));
    assert(slab2 != NULL);
    assert(slab1 != slab2);

    tsa_destroy(h);
    printf("test_basic_allocation passed.\n");
}

void test_alignment() {
    printf("Running test_alignment...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    void* p1 = tsa_mem_pool_alloc(h, 1);
    void* p2 = tsa_mem_pool_alloc(h, 1);

    uintptr_t u1 = (uintptr_t)p1;
    uintptr_t u2 = (uintptr_t)p2;

    printf("p1: %p, p2: %p, diff: %ld\n", p1, p2, u2 - u1);
    assert(u1 % 64 == 0);
    assert(u2 % 64 == 0);
    assert(u2 - u1 == 64);  // Should be aligned to 64 bytes

    tsa_destroy(h);
    printf("test_alignment passed.\n");
}

void test_data_persistence() {
    printf("Running test_data_persistence...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    test_slab_t* s = (test_slab_t*)tsa_mem_pool_alloc(h, sizeof(test_slab_t));
    s->id = 0xDEADBEEF;
    strcpy(s->data, "Hello Slab");

    // Allocate something else
    tsa_mem_pool_alloc(h, 100);

    // Verify first slab
    assert(s->id == 0xDEADBEEF);
    assert(strcmp(s->data, "Hello Slab") == 0);

    tsa_destroy(h);
    printf("test_data_persistence passed.\n");
}

void test_pool_exhaustion() {
    printf("Running test_pool_exhaustion...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    int count = 0;
    while (tsa_mem_pool_alloc(h, 1024) != NULL) {
        count++;
        if (count > 100000) break;
    }

    printf("Allocated %d slabs (1KB each) before exhaustion.\n", count);
    assert(count > 0);

    tsa_destroy(h);
    printf("test_pool_exhaustion passed.\n");
}

int main() {
    test_basic_allocation();
    test_alignment();
    test_data_persistence();
    test_pool_exhaustion();
    return 0;
}
