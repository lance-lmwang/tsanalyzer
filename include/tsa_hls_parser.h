#ifndef TSA_HLS_PARSER_H
#define TSA_HLS_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_URL_LEN 2048

typedef struct {
    char url[MAX_URL_LEN];
    double duration;
    uint64_t sequence;
    bool discontinuity;
} tsa_hls_segment_t;

typedef struct {
    char master_url[MAX_URL_LEN];
    uint32_t version;
    uint64_t target_duration;
    uint64_t media_sequence;
    bool is_live;

    // Callback for each segment found
    void (*on_segment)(void *user_data, const tsa_hls_segment_t *seg);
    void *user_data;
} tsa_hls_context_t;

/**
 * Parse M3U8 content and trigger callbacks for segments.
 * @return 0 on success, negative on error.
 */
int tsa_hls_parse_m3u8(tsa_hls_context_t *ctx, const char *data, size_t len);

/**
 * Resolve relative URL to absolute URL.
 */
void tsa_hls_resolve_url(const char *base_url, const char *rel_url, char *out_url, size_t out_sz);

#endif
