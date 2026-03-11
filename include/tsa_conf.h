#ifndef TSA_CONF_H
#define TSA_CONF_H

#include <stdbool.h>
#include <stdint.h>

#include "tsa.h"

#define MAX_OUTPUTS 8
#define MAX_STREAMS_IN_CONF 256

typedef struct {
    char id[TSA_ID_MAX];
    tsa_config_t cfg;
} tsa_stream_conf_t;

typedef struct {
    // Global settings
    int http_listen_port;
    int srt_listen_port;
    int worker_threads;
    int worker_slice_us;
    bool shm_enabled;
    char log_format[16];
    char api_secret[128];

    // VHost templates (we use __default__ as index 0)
    tsa_config_t vhost_default;

    // Stream instances
    tsa_stream_conf_t streams[MAX_STREAMS_IN_CONF];
    int stream_count;
} tsa_full_conf_t;

/**
 * Load and parse a hierarchical configuration file.
 * @return 0 on success, negative on error.
 */
int tsa_conf_load(tsa_full_conf_t* conf, const char* filename);

/**
 * Get effective configuration for a stream by ID.
 * Merges stream-specific settings with vhost defaults.
 */
tsa_config_t* tsa_conf_get_stream(tsa_full_conf_t* conf, const char* id);

/**
 * Start a background thread to watch for configuration file changes.
 */
void tsa_conf_watcher_start(tsa_full_conf_t* conf, const char* filename);

#endif
