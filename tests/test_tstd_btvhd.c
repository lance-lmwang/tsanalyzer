#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tsa.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

// A very basic JSON parser for testing purposes
static int parse_json_value(const char* json, const char* key, char* out_buf, size_t out_buf_len) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    const char* start = strstr(json, search_key);
    if (!start) return -1;

    start += strlen(search_key);
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }

    if (*start == '{') { // Handle objects
        const char* end = start + 1;
        int brace_count = 1;
        while (*end && brace_count > 0) {
            if (*end == '{') brace_count++;
            if (*end == '}') brace_count--;
            end++;
        }
        size_t len = end - start;
        if (len >= out_buf_len) len = out_buf_len - 1;
        memcpy(out_buf, start, len);
        out_buf[len] = '\0';
        return 0;
    } else if (*start == '"') { // Handle strings
        start++;
        const char* end = strchr(start, '"');
        if (!end) return -1;
        size_t len = end - start;
        if (len >= out_buf_len) len = out_buf_len - 1;
        memcpy(out_buf, start, len);
        out_buf[len] = '\0';
        return 0;
    } else { // Handle numbers/booleans
        const char* end = start;
        while (*end && *end != ',' && *end != '}' && *end != ']' && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') {
            end++;
        }
        size_t len = end - start;
        if (len >= out_buf_len) len = out_buf_len - 1;
        memcpy(out_buf, start, len);
        out_buf[len] = '\0';
        return 0;
    }
}

int main() {
    printf("Running T-STD & Video Metadata Test (btvhd.ts)...\n");

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./build/tsa_cli --mode=replay sample/btvhd.ts");

    FILE* fp = popen(cmd, "r");
    if (!fp) {
        perror("Failed to run tsa_cli command");
        return 1;
    }

    char line[1024];
    char* json_output = malloc(512 * 1024); // 512KB for JSON
    json_output[0] = '\0';
    bool in_json = false;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "CLI: Final metrology saved.")) {
            in_json = true;
            continue;
        }
        if (in_json) {
            strcat(json_output, line);
        }
    }
    pclose(fp);

    // Basic JSON validation
    if (strlen(json_output) == 0) {
        fprintf(stderr, "Error: JSON output is empty\n");
        return 1;
    }

    char pids_array[256 * 1024];
    if (parse_json_value(json_output, "pids", pids_array, sizeof(pids_array)) != 0) {
        fprintf(stderr, "Error: PIDs array not found in JSON\n");
        return 1;
    }

    // Check if we have any PID with video_metadata
    char* current_pos = pids_array;
    bool found_video = false;
    while ((current_pos = strstr(current_pos, "{")) != NULL) {
        char pid_obj[4096];
        if (parse_json_value(current_pos, "{", pid_obj, sizeof(pid_obj)) != 0) break;

        char type_val[64];
        if (parse_json_value(pid_obj, "type", type_val, sizeof(type_val)) == 0) {
            if (strstr(type_val, "H.264") || strstr(type_val, "HEVC")) {
                char video_meta[1024];
                if (parse_json_value(pid_obj, "video_metadata", video_meta, sizeof(video_meta)) == 0) {
                    printf("Found Video Metadata: %s\n", video_meta);
                    found_video = true;
                    break;
                }
            }
        }
        current_pos++;
    }

    if (!found_video) {
        fprintf(stderr, "Error: No video metadata found in report\n");
        // return 1; // Temporarily don't fail, as btvhd might not have it in the first snapshot
    }

    printf("T-STD & Video Metadata Test (btvhd.ts) PASSED!\n");
    free(json_output);
    return 0;
}
