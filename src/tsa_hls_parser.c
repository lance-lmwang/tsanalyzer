#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa_hls_parser.h"

static const char* find_line_end(const char *s, const char *end) {
    while (s < end && *s != '\n' && *s != '\r') s++;
    return s;
}

static const char* skip_whitespace(const char *s, const char *end) {
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    return s;
}

void tsa_hls_resolve_url(const char *base_url, const char *rel_url, char *out_url, size_t out_sz) {
    if (strncmp(rel_url, "http", 4) == 0) {
        strncpy(out_url, rel_url, out_sz - 1);
        return;
    }

    // Basic URL concatenation logic
    // Find the last slash in base_url
    const char *last_slash = strrchr(base_url, '/');
    if (!last_slash) {
        strncpy(out_url, rel_url, out_sz - 1);
        return;
    }

    size_t base_len = (size_t)(last_slash - base_url) + 1;
    if (rel_url[0] == '/') {
        // Find domain root
        const char *p = strstr(base_url, "://");
        if (p) {
            p += 3;
            const char *domain_end = strchr(p, '/');
            if (domain_end) base_len = (size_t)(domain_end - base_url);
        }
    }

    snprintf(out_url, out_sz, "%.*s%s", (int)base_len, base_url, rel_url);
}

int tsa_hls_parse_m3u8(tsa_hls_context_t *ctx, const char *data, size_t len) {
    const char *p = data;
    const char *end = data + len;

    if (len < 7 || memcmp(data, "#EXTM3U", 7) != 0) return -1;

    double current_duration = 0;
    uint64_t current_seq = 0;
    bool is_discontinuity = false;

    while (p < end) {
        p = skip_whitespace(p, end);
        if (p >= end) break;

        const char *line_end = find_line_end(p, end);
        size_t line_len = (size_t)(line_end - p);

        if (strncmp(p, "#EXTINF:", 8) == 0) {
            current_duration = atof(p + 8);
        } else if (strncmp(p, "#EXT-X-TARGETDURATION:", 22) == 0) {
            ctx->target_duration = (uint64_t)atoll(p + 22);
        } else if (strncmp(p, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0) {
            ctx->media_sequence = (uint64_t)atoll(p + 22);
            current_seq = ctx->media_sequence;
        } else if (strncmp(p, "#EXT-X-DISCONTINUITY", 20) == 0) {
            is_discontinuity = true;
        } else if (strncmp(p, "#EXT-X-ENDLIST", 14) == 0) {
            ctx->is_live = false;
        } else if (*p != '#') {
            // This is a URL
            tsa_hls_segment_t seg;
            char rel_url[MAX_URL_LEN];
            snprintf(rel_url, sizeof(rel_url), "%.*s", (int)line_len, p);

            tsa_hls_resolve_url(ctx->master_url, rel_url, seg.url, sizeof(seg.url));
            seg.duration = current_duration;
            seg.sequence = current_seq++;
            seg.discontinuity = is_discontinuity;

            if (ctx->on_segment) ctx->on_segment(ctx->user_data, &seg);

            is_discontinuity = false; // Reset
        }

        p = line_end;
    }

    return 0;
}
