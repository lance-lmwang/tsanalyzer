#ifndef TSP_H
#define TSP_H

#include <netinet/in.h>
#include <pthread.h>
#include <srt.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "tsa_bitstream.h"
#include "tsa_pcr_clock.h"

#define TS_PACKET_SIZE 188
#define TS_PACKET_BITS (TS_PACKET_SIZE * 8)
#define RING_BUFFER_SIZE (16 * 1024)

typedef enum { TSPACER_MODE_BASIC = 0, TSPACER_MODE_PCR, TSPACER_MODE_CBR } tsp_mode_t;

typedef struct {
    uint64_t bitrate;
    const char* dest_ip;
    int port;
    int ts_per_udp;
    int cpu_core;
    const char* url;
    uint16_t pcr_pid;
    tsp_mode_t mode;
    void (*stats_cb)(void* handle, void* stats, void* user_data);
    void* user_data;
} tsp_config_t;

typedef struct {
    uint64_t detected_bitrate;
    uint64_t total_packets;
    uint64_t pps;
    uint64_t timestamp_ns;
} tsp_stats_t;

typedef struct tsp_handle {
    tsp_config_t cfg;
    uint8_t* ring_buffer;
    uint64_t* ts_buffer;
    int fd;
    struct sockaddr_in dest_addr;
    SRTSOCKET srt_sock;
    bool srt_enabled;

    _Atomic uint64_t head;
    _Atomic uint64_t tail;
    _Atomic bool running;
    pthread_t thread;

    _Atomic uint64_t total_ts_sent;
    _Atomic uint64_t total_udp_sent;

    /* Professional Pacer State */
    uint64_t base_pcr_ticks;
    uint64_t base_wall_ns;
    uint64_t last_scheduled_ns;
    uint64_t last_pcr_val_tx;
    uint64_t last_pcr_wall_ns;
    uint64_t pkts_since_pcr;
    uint64_t estimated_bitrate;
    uint16_t locked_pcr_pid;
    time_t last_pcr_reset_time;

    uint64_t last_ns;
    uint64_t last_t;

    tsa_pcr_clock_t clk;
} tsp_handle_t;

/* --- Ingest Structures (Matching srt_ingest.c) --- */
typedef struct {
    SRTSOCKET sock;
} ts_ingest_srt_t;

typedef struct {
    int fd;
} ts_ingest_udp_t;

ts_ingest_srt_t* ts_ingest_srt_create(const char* url);
void ts_ingest_srt_destroy(ts_ingest_srt_t* ingest);
int ts_ingest_srt_recv(ts_ingest_srt_t* ingest, uint8_t* buf, int sz);
struct tsa_srt_stats;
int ts_ingest_srt_get_stats(ts_ingest_srt_t* ingest, struct tsa_srt_stats* srt);

ts_ingest_udp_t* ts_ingest_udp_create(const char* ip, uint16_t port);
void ts_ingest_udp_destroy(ts_ingest_udp_t* ingest);
int ts_ingest_udp_recv(ts_ingest_udp_t* ingest, uint8_t* buf, int sz);

typedef struct spsc_ring spsc_ring_t;

tsp_handle_t* tsp_create(const tsp_config_t* cfg);
void tsp_destroy(tsp_handle_t* h);
int tsp_start(tsp_handle_t* h);
int tsp_stop(tsp_handle_t* h);
int tsp_enqueue(tsp_handle_t* h, const uint8_t* ts_packets, size_t count);

uint64_t tsp_get_detected_bitrate(tsp_handle_t* h);
uint64_t tsp_get_estimated_bitrate(tsp_handle_t* h);
uint64_t tsp_get_total_packets(tsp_handle_t* h);
int tsp_get_stats(tsp_handle_t* h, uint64_t* total, int64_t* max_j, int64_t* min_j, uint64_t* drops, uint64_t* det_rate,
                  uint64_t* pps);
int tsp_get_stats_snapshot(tsp_handle_t* h, tsp_stats_t* s);
uint64_t tsp_get_bitrate(tsp_handle_t* h);
pthread_t tsp_get_thread(tsp_handle_t* h);

/* Unit Test Helpers */
spsc_ring_t* spsc_ring_create(size_t sz);
void spsc_ring_destroy(spsc_ring_t* r);
int spsc_ring_push(spsc_ring_t* r, const uint8_t* data, size_t sz);
int spsc_ring_pop(spsc_ring_t* r, uint8_t* data, size_t sz);

int parse_url_ext(const char* url, char* host, int* port, int* is_listener, int* latency, char* passphrase,
                  int* pbkeylen);

#endif
