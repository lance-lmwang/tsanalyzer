#ifndef TSA_HISTOGRAM_H
#define TSA_HISTOGRAM_H

#include <stdbool.h>
#include <stdint.h>

#define TSA_HIST_RESOLUTION_MS 10
#define TSA_HIST_WINDOW_MS 1000
#define TSA_HIST_BUCKETS (TSA_HIST_WINDOW_MS / TSA_HIST_RESOLUTION_MS)

typedef struct {
    uint64_t bits_in_bucket[TSA_HIST_BUCKETS];
    uint64_t last_bucket_ns;
    uint32_t current_bucket_idx;

    uint64_t min_bps;
    uint64_t max_bps;
    uint64_t p99_bps;

    bool initialized;
} tsa_histogram_t;

void tsa_hist_init(tsa_histogram_t* h);
void tsa_hist_add_packet(tsa_histogram_t* h, uint64_t now_ns, uint32_t bits);
void tsa_hist_update(tsa_histogram_t* h, uint64_t now_ns);

#endif
