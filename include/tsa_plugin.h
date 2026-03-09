#ifndef TSA_PLUGIN_H
#define TSA_PLUGIN_H

#include "tsa_stream.h"

#define MAX_TSA_PLUGINS 16

struct tsa_handle;

/**
 * Plugin Operations Interface
 */
typedef struct tsa_plugin_ops_s {
    const char* name;

    // Allocate and initialize the plugin instance
    // context_buf is a pointer to h->plugins[slot].context, MUST be used for in-place init instead of calloc
    void* (*create)(void* config_or_parent, void* context_buf);

    // Destroy the plugin (do not free the instance memory as it's owned by the handle)
    void (*destroy)(void* self);

    // Process a single TS packet. Handlers should read h->current_res for parsed headers.
    void (*on_ts)(void* self, const uint8_t* pkt);

    // Optional: Get the stream node of this plugin (to attach to a parent) - deprecated in flat dispatch
    tsa_stream_t* (*get_stream)(void* self);

    // Optional: Reset internal state
    void (*reset)(void* self);

    // Optional: Commit/snapshot the current measurements.
    void (*commit)(void* self, uint64_t now_ns);

} tsa_plugin_ops_t;

/**
 * Registry Management
 */
void tsa_plugins_init_registry(void);
void tsa_plugin_register(tsa_plugin_ops_t* ops);
tsa_plugin_ops_t* tsa_plugin_find(const char* name);

/**
 * Handle Management
 */
void tsa_plugins_attach_builtin(struct tsa_handle* h);
void tsa_plugin_attach_instance(struct tsa_handle* h, tsa_plugin_ops_t* ops);
void tsa_plugins_destroy_all(struct tsa_handle* h);

#endif  // TSA_PLUGIN_H
