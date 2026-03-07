#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "tsa_hls_parser.h"

typedef struct {
    char *data;
    size_t size;
} memory_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    memory_t *mem = (memory_t *)userp;
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

static void on_segment_found(void *user_data, const tsa_hls_segment_t *seg) {
    printf("  [SEG] Seq: %lu, Duration: %.2fs, URL: %s\n", seg->sequence, seg->duration, seg->url);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <m3u8_url>\n", argv[0]);
        return 1;
    }

    const char *url = argv[1];
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();
    if (!curl) return 1;

    memory_t chunk = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TsAnalyzer-HLS/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    printf("Fetching M3U8: %s\n", url);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        // Fallback to local file
        FILE *fp = fopen(url, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            chunk.size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            chunk.data = malloc(chunk.size + 1);
            fread(chunk.data, 1, chunk.size, fp);
            chunk.data[chunk.size] = 0;
            fclose(fp);
            printf("Loaded local file: %zu bytes\n", chunk.size);
        } else {
            fprintf(stderr, "curl failed and no local file found: %s\n", curl_easy_strerror(res));
        }
    }

    if (chunk.data) {
        tsa_hls_context_t ctx = {0};
        strncpy(ctx.master_url, url, sizeof(ctx.master_url) - 1);
        ctx.is_live = true;
        ctx.on_segment = on_segment_found;

        if (tsa_hls_parse_m3u8(&ctx, chunk.data, chunk.size) == 0) {
            printf("Parse successful. Target Duration: %lu\n", ctx.target_duration);
        } else {
            printf("Parse failed.\n");
        }
    }

    free(chunk.data);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
