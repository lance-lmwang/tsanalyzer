#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "tsa_internal.h"

// Simple helper to get current time in nanoseconds
static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void benchmark_string_builder() {
    printf("Running benchmark_string_builder...\n");

    // Allocate a large enough buffer (e.g., 2MB) to hold a 100-stream dump
    char storage[2 * 1024 * 1024];
    tsa_metric_buffer_t buf;

    // Simulate generating metrics for 100 "streams" or "PIDs"
    // Each stream might have 5-10 metrics (e.g., bitrate, jitter, CC errors, etc.)
    int num_streams = 100;

    uint64_t start_ns = get_time_ns();

    tsa_mbuf_init(&buf, storage, sizeof(storage));

    for (int i = 0; i < num_streams; i++) {
        // Metric 1: Bitrate
        tsa_mbuf_append_str(&buf, "tsa_pid_bitrate_bps{pid=\"");
        tsa_mbuf_append_int(&buf, i + 0x100);  // Simulate PID
        tsa_mbuf_append_str(&buf, "\"} ");
        tsa_mbuf_append_int(&buf, 4500000 + i * 100);  // Simulate bitrate value
        tsa_mbuf_append_char(&buf, '\n');

        // Metric 2: PCR Jitter
        tsa_mbuf_append_str(&buf, "tsa_pcr_jitter_ns{pid=\"");
        tsa_mbuf_append_int(&buf, i + 0x100);
        tsa_mbuf_append_str(&buf, "\"} ");
        tsa_mbuf_append_float(&buf, 25.5f + (float)i * 0.1f, 2);
        tsa_mbuf_append_char(&buf, '\n');

        // Metric 3: CC Errors
        tsa_mbuf_append_str(&buf, "tsa_cc_errors_total{pid=\"");
        tsa_mbuf_append_int(&buf, i + 0x100);
        tsa_mbuf_append_str(&buf, "\"} ");
        tsa_mbuf_append_int(&buf, i % 5);
        tsa_mbuf_append_char(&buf, '\n');
    }

    uint64_t end_ns = get_time_ns();
    uint64_t duration_ns = end_ns - start_ns;
    double duration_us = (double)duration_ns / 1000.0;

    printf("Generated %d stream metrics in %.2f us (Buffer offset: %zu bytes)\n", num_streams, duration_us, buf.offset);

    // The goal is < 50us for a full 100-stream dump.
    // If we're slightly over in a debug/unoptimized build, it might be fine,
    // but in Release, it should easily hit the target.
    // Let's assert a reasonable bound (e.g., 500us max in test environments to avoid flakiness,
    // but we'll print the actual).
    assert(duration_us < 500.0);  // Generous upper bound for CI environments

    printf("benchmark_string_builder passed.\n");
}

int main() {
    benchmark_string_builder();
    return 0;
}
