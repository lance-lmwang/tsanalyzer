#include "tsa_histogram.h"

#include <stdlib.h>
#include <string.h>

#define NS_PER_MS 1000000ULL
#define NS_PER_RESOLUTION (TSA_HIST_RESOLUTION_MS* NS_PER_MS)

void tsa_hist_init(tsa_histogram_t* h) {
    if (!h) return;
    memset(h, 0, sizeof(*h));
}

static int compare_uint64(const void* a, const void* b) {
    uint64_t arg1 = *(const uint64_t*)a;
    uint64_t arg2 = *(const uint64_t*)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

static void tsa_hist_update_metrics(tsa_histogram_t* h) {
    if (!h) return;
    uint64_t bitrates[TSA_HIST_BUCKETS];
    uint64_t min = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t max = 0;

    for (int i = 0; i < TSA_HIST_BUCKETS; i++) {
        uint64_t bps = h->bits_in_bucket[i] * 1000 / TSA_HIST_RESOLUTION_MS;
        bitrates[i] = bps;
        if (bps < min) min = bps;
        if (bps > max) max = bps;
    }

    qsort(bitrates, TSA_HIST_BUCKETS, sizeof(uint64_t), compare_uint64);

    h->min_bps = min;
    h->max_bps = max;
    h->p99_bps = bitrates[TSA_HIST_BUCKETS - 2]; /* 99th value in sorted array */
}

void tsa_hist_add_packet(tsa_histogram_t* h, uint64_t now_ns, uint32_t bits) {
    if (!h) return;
    if (!h->initialized) {
        h->last_bucket_ns = now_ns;
        h->current_bucket_idx = 0;
        h->initialized = true;
    }

    tsa_hist_update(h, now_ns);
    h->bits_in_bucket[h->current_bucket_idx] += bits;
}

void tsa_hist_update(tsa_histogram_t* h, uint64_t now_ns) {
    if (!h || !h->initialized) return;

    if (now_ns < h->last_bucket_ns) {
        return;
    }

    uint64_t elapsed = now_ns - h->last_bucket_ns;
    if (elapsed >= NS_PER_RESOLUTION) {
        uint32_t buckets_to_skip = (uint32_t)(elapsed / NS_PER_RESOLUTION);

        if (buckets_to_skip >= TSA_HIST_BUCKETS) {
            // Gap is larger than the entire window, clear everything
            memset(h->bits_in_bucket, 0, sizeof(h->bits_in_bucket));
            h->current_bucket_idx = 0;
            h->last_bucket_ns = now_ns;
        } else {
            for (uint32_t i = 0; i < buckets_to_skip; i++) {
                h->current_bucket_idx = (h->current_bucket_idx + 1) % TSA_HIST_BUCKETS;
                h->bits_in_bucket[h->current_bucket_idx] = 0;
            }
            h->last_bucket_ns += (uint64_t)buckets_to_skip * NS_PER_RESOLUTION;
        }

        tsa_hist_update_metrics(h);
    }
}
