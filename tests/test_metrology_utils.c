#include <assert.h>
#include <stdio.h>
#include <time.h>

#include "tsa_internal.h"

void test_time_conversion() {
    printf("Testing 128-bit time conversion...\n");

    struct timespec ts = {.tv_sec = 1234567890, .tv_nsec = 500000000};
    __int128_t ns = ts_time_to_ns128(ts);

    // 1234567890 * 10^9 + 500,000,000
    __int128_t expected = (__int128_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
    assert(ns == expected);

    struct timespec back = ns128_to_timespec(ns);
    assert(back.tv_sec == ts.tv_sec);
    assert(back.tv_nsec == ts.tv_nsec);

    printf("128-bit time conversion tests passed.\n");
}

void test_fixed_point() {
    printf("Testing 64.64 fixed-point math...\n");

    double val1 = 123.456;
    double val2 = 78.901;

    q64_64 q1 = TO_Q64_64(val1);
    q64_64 q2 = TO_Q64_64(val2);

    q64_64 sum = q1 + q2;
    q64_64 diff = q1 - q2;

    double sum_d = FROM_Q64_64(sum);
    double diff_d = FROM_Q64_64(diff);

    assert(sum_d > 202.356 && sum_d < 202.358);
    assert(diff_d > 44.554 && diff_d < 44.556);

    printf("64.64 fixed-point math tests passed.\n");
}

int main() {
    test_time_conversion();
    test_fixed_point();
    return 0;
}
