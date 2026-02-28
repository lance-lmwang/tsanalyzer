#ifndef TSP_H
#define TSP_H

#include <netinet/in.h>
#include <pthread.h>
#include <srt.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define TS_PACKET_SIZE 188
#ifndef RING_BUFFER_SIZE
#define RING_BUFFER_SIZE (1024 * 128)
#endif
#define NS_PER_SEC 1000000000ULL
#define INVALID_PCR 0xFFFFFFFFFFFFFFFFULL

typedef enum {
    TSPACER_MODE_PCR,
    TSPACER_MODE_ETF,
    TSPACER_MODE_CBR,
    TSPACER_MODE_BASIC  // Added for pacing test
} tsp_mode_t;

typedef struct {
    uint64_t total_packets;
    int64_t max_jitter_ns;
    int64_t min_jitter_ns;
    uint64_t drop_events;
    uint64_t detected_bitrate;
    uint64_t pps;
    uint64_t timestamp_ns;
} tsp_stats_t;

typedef struct tsp_handle tsp_handle_t;
typedef void (*tsp_stats_cb_t)(tsp_handle_t* h, const tsp_stats_t* stats, void* user_data);

typedef struct {
    tsp_mode_t mode;
    uint64_t bitrate;
    uint32_t ts_per_udp;
    int cpu_core;
    const char* srt_url;
    const char* dest_ip;
    uint16_t port;
    tsp_stats_cb_t stats_cb;
    void* user_data;
} tsp_config_t;

struct tsp_handle {
    tsp_config_t cfg;
    alignas(64) _Atomic uint64_t head;
    alignas(64) _Atomic uint64_t tail;
    uint8_t* ring_buffer;
    _Atomic bool running;
    pthread_t thread;
    int fd;
    struct sockaddr_in dest_addr;
    SRTSOCKET srt_sock;
    bool srt_enabled;
    uint8_t stuffing_packet[TS_PACKET_SIZE];
    _Atomic uint64_t total_udp_packets;
    _Atomic uint64_t detected_bitrate;

    // Legacy members for tests
    uint64_t pcr_base;
    uint64_t sys_time_base;
    uint64_t last_pcr_val_tx;
    uint64_t byte_offset_base;
};

/* API */
tsp_handle_t* tsp_create(const tsp_config_t* cfg);
int tsp_start(tsp_handle_t* h);
int tsp_stop(tsp_handle_t* h);
void tsp_destroy(tsp_handle_t* h);
int tsp_enqueue(tsp_handle_t* h, const uint8_t* ts_packets, size_t count);
uint64_t tsp_get_detected_bitrate(tsp_handle_t* h);
uint64_t tsp_get_total_packets(tsp_handle_t* h);
uint64_t tsp_get_udp_rate_scaled(tsp_handle_t* h);
void tsp_update_bitrate(tsp_handle_t* h, uint64_t new_bitrate);
uint64_t tsp_get_bitrate(tsp_handle_t* h);
pthread_t tsp_get_thread(tsp_handle_t* h);
int tsp_get_stats(tsp_handle_t* h, uint64_t* total, int64_t* max_j, int64_t* min_j, uint64_t* drops, uint64_t* det_rate,
                  uint64_t* pps);
int tsp_get_stats_snapshot(tsp_handle_t* h, tsp_stats_t* snap);
uint64_t calculate_target_time(tsp_handle_t* h, uint64_t pcr, uint64_t byte_off, uint64_t now);

// SRT Helpers
struct tsa_srt_stats;
typedef struct ts_ingest_srt ts_ingest_srt_t;
ts_ingest_srt_t* ts_ingest_srt_create(const char* url);
void ts_ingest_srt_destroy(ts_ingest_srt_t* ingest);
int ts_ingest_srt_recv(ts_ingest_srt_t* ingest, uint8_t* buf, int sz);
int ts_ingest_srt_get_stats(ts_ingest_srt_t* ingest, struct tsa_srt_stats* srt);

typedef struct ts_ingest_udp ts_ingest_udp_t;
ts_ingest_udp_t* ts_ingest_udp_create(const char* ip, uint16_t port);
void ts_ingest_udp_destroy(ts_ingest_udp_t* ingest);
int ts_ingest_udp_recv(ts_ingest_udp_t* ingest, uint8_t* buf, int sz);

/* SPSC Ring Mock for tests */
typedef struct spsc_ring spsc_ring_t;
spsc_ring_t* spsc_ring_create(size_t sz);
void spsc_ring_destroy(spsc_ring_t* r);
int spsc_ring_push(spsc_ring_t* r, const uint8_t* data, size_t sz);
int spsc_ring_pop(spsc_ring_t* r, uint8_t* data, size_t sz);

int parse_srt_url_ext(const char* url, char* host, int* port, int* is_listener, int* latency, char* passphrase,
                      int* pbkeylen);

#endif
