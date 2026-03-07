#ifndef TSA_SOURCE_H
#define TSA_SOURCE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "tsa_event.h"

typedef enum {
    TSA_SOURCE_UDP,
    TSA_SOURCE_SRT,
    TSA_SOURCE_FILE,
    TSA_SOURCE_PCAP,
    TSA_SOURCE_HLS
} tsa_source_type_t;

typedef struct tsa_source tsa_source_t;

typedef struct {
    /**
     * Called when a new packet (or batch of packets) is received.
     * @param pkts Pointer to the buffer containing TS packets (multiples of 188).
     * @param count Number of 188-byte packets in the buffer.
     */
    void (*on_packets)(void* user_data, const uint8_t* pkts, int count, uint64_t now_ns);

    /**
     * Called on source errors or status changes (e.g., EOF, connection lost).
     */
    void (*on_status)(void* user_data, int status_code, const char* msg);
} tsa_source_callbacks_t;

/* HLS Ingest Internal API */
void* tsa_hls_ingest_start(const char *url, const tsa_source_callbacks_t *cbs, void *user_data);
void tsa_hls_ingest_stop(void *handle);

/**
 * Create a new source instance.
 * @param type The type of source (UDP, SRT, etc.)
 * @param url The connection URL or identifier.
 * @param cbs Callbacks for data and status.
 * @param user_data Opaque pointer passed to callbacks.
 */
tsa_source_t* tsa_source_create(tsa_source_type_t type, const char* url, const char* filter_ip, int filter_port, const tsa_source_callbacks_t* cbs, void* user_data);

/**
 * Set a reactor for the source. If set, the source will use the reactor
 * instead of creating its own thread.
 */
void tsa_source_set_reactor(tsa_source_t* src, tsa_reactor_t* reactor);

/**
 * Set pacing for the source (useful for files/pcap).
 */
void tsa_source_set_pacing(tsa_source_t* src, bool enabled);

/**
 * Destroy the source and release resources.
 */
void tsa_source_destroy(tsa_source_t* src);

/**
 * Start receiving data from the source.
 */
int tsa_source_start(tsa_source_t* src);

/**
 * Stop receiving data.
 */
int tsa_source_stop(tsa_source_t* src);

#endif
