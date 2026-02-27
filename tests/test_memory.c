#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>

#include "tsp.h"

void check_vmlck() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return;
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmLck:", 6) == 0) {
            printf("Memory status: %s", line);
            found = true;
            break;
        }
    }
    fclose(f);
    if (!found) printf("VmLck not found in /proc/self/status\n");
}

int main() {
    tsp_config_t cfg = {0};
    cfg.bitrate = 1000000;
    cfg.dest_ip = "127.0.0.1";
    cfg.port = 1234;
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;

    struct rusage usage_before, usage_after;
    getrusage(RUSAGE_SELF, &usage_before);

    tsp_handle_t* h = tsp_create(&cfg);
    assert(h != NULL);

    getrusage(RUSAGE_SELF, &usage_after);

    printf("Minor faults before: %ld, after: %ld\n", usage_before.ru_minflt, usage_after.ru_minflt);
    check_vmlck();

    tsp_destroy(h);
    printf("Memory hardening test completed.\n");
    return 0;
}
