#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_hls_parser.h"

static int g_seg_count = 0;
static void on_segment(void *user_data, const tsa_hls_segment_t *seg) {
    (void)g_seg_count;
    assert(seg->sequence == (uint64_t)expected_seq);
    if (g_seg_count == 0) {
        assert(strstr(seg->url, "segment_101.ts") != NULL);
    }
    g_seg_count++;
}

int main() {
    const char *m3u8 =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXT-X-MEDIA-SEQUENCE:101\n"
        "#EXTINF:9.0,\n"
        "segment_101.ts\n"
        "#EXTINF:9.0,\n"
        "segment_102.ts\n";

    tsa_hls_context_t ctx = {0};
    strcpy(ctx.master_url, "http://example.com/live/index.m3u8");
    ctx.on_segment = on_segment;

    printf("Testing HLS Parser...\n");
    int res __attribute__((unused)) = tsa_hls_parse_m3u8(&ctx, m3u8, strlen(m3u8));
    assert(res == 0);
    assert(ctx.media_sequence == 101);
    assert(ctx.target_duration == 10);
    assert(g_seg_count == 2);

    // Test URL resolution
    char resolved[2048];
    tsa_hls_resolve_url("http://a.com/b/c.m3u8", "d.ts", resolved, sizeof(resolved));
    assert(strcmp(resolved, "http://a.com/b/d.ts") == 0);

    tsa_hls_resolve_url("http://a.com/b/c.m3u8", "/d.ts", resolved, sizeof(resolved));
    assert(strcmp(resolved, "http://a.com/d.ts") == 0);

    printf("HLS Parser Test: PASSED\n");
    return 0;
}
