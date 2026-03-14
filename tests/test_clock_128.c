#include <assert.h>
#include <stdio.h>
#include <time.h>

#include "tsa_internal.h"

void test_now_ns128() {
    printf("Running test_now_ns128...\n");
    int128_t t1 = ts_now_ns128();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int128_t t2 = ts_time_to_ns128(ts);
    (void)t1;
    (void)t2;

    assert(t1 > 0);
    assert(t2 >= t1);
    assert(t2 - t1 < 100000000);  // within 100ms
    printf("test_now_ns128 passed.\n");
}

int main() {
    test_now_ns128();
    return 0;
}
