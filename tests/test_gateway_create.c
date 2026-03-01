#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "tsa.h"

void* terminator_thread(void* arg) {
    (void)arg;
    sleep(5); // Hard limit for this specific test
    printf("Test stuck for 5s, forcing exit...\n");
    exit(1);
}

int main() {
    pthread_t tid;
    pthread_create(&tid, NULL, terminator_thread, NULL);
    pthread_detach(tid);

    tsa_gateway_config_t cfg = {0};
    // Use a non-listening URL to avoid blocking on accept
    cfg.pacing.srt_url = "srt://127.0.0.1:9005?mode=caller&timeout=1000";
    cfg.pacing.dest_ip = "127.0.0.1";
    cfg.pacing.port = 0;
    cfg.analysis.is_live = true;
    cfg.analysis.enable_forensics = true;

    printf("Attempting to create gateway (caller mode)...\n");
    // This should fail fast if no server is listening
    tsa_gateway_t* gw = tsa_gateway_create(&cfg);

    if (gw) {
        printf("SUCCESS: Gateway created.\n");
        tsa_gateway_destroy(gw);
    } else {
        printf("Gateway creation failed as expected (or due to no listener).\n");
    }

    printf("Test finished.\n");
    return 0;
}
