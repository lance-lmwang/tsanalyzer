#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "tsa_source.h"
#include "tsa_event.h"

static int g_packet_count = 0;

static void on_packets(void* user_data, const uint8_t* pkts, int count, uint64_t now_ns) {
    (void)user_data;
    (void)pkts;
    (void)now_ns;
    g_packet_count += count;
}

static void on_status(void* user_data, int status_code, const char* msg) {
    (void)user_data;
    printf("Source Status: %d (%s)\n", status_code, msg);
}

static void* reactor_thread(void* arg) {
    tsa_reactor_t* reactor = (tsa_reactor_t*)arg;
    tsa_reactor_run(reactor);
    return NULL;
}

void test_source_with_reactor() {
    printf("Testing Source with Reactor Event Loop...\n");
    
    tsa_reactor_t* reactor = tsa_reactor_create();
    assert(reactor != NULL);

    tsa_source_callbacks_t cbs = {
        .on_packets = on_packets,
        .on_status = on_status
    };

    tsa_source_t* src = tsa_source_create(TSA_SOURCE_UDP, "127.0.0.1:9999", NULL, 0, &cbs, NULL);
    assert(src != NULL);

    tsa_source_set_reactor(src, reactor);
    int rc = tsa_source_start(src);
    if (rc != 0) {
        fprintf(stderr, "ERROR: tsa_source_start failed with %d\n", rc);
        exit(1);
    }

    // Send a packet to trigger the reactor
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9999);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    uint8_t pkt[188];
    memset(pkt, 0x47, 188);
    sendto(sock, pkt, 188, 0, (struct sockaddr*)&sa, sizeof(sa));
    close(sock);

    pthread_t tid;
    pthread_create(&tid, NULL, reactor_thread, reactor);

    // Wait a bit for the packet to be processed
    int retries = 50;
    while (retries-- > 0 && g_packet_count == 0) {
        usleep(20000);
    }

    printf("Final packet count: %d\n", g_packet_count);
    if (g_packet_count == 0) {
        fprintf(stderr, "ERROR: No packets received via reactor!\n");
        exit(1);
    }

    tsa_reactor_stop(reactor);
    pthread_join(tid, NULL);

    tsa_source_stop(src);
    tsa_source_destroy(src);
    tsa_reactor_destroy(reactor);
    
    printf("Source with Reactor OK.\n");
}

int main() {
    test_source_with_reactor();
    return 0;
}