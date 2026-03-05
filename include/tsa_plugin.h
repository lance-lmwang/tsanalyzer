#ifndef TSA_PLUGIN_H
#define TSA_PLUGIN_H

#include "tsa_stream.h"

typedef struct tsa_plugin_ops_s {
    const char* name;
    
    // Allocate and initialize the plugin instance
    void* (*create)(void* config_or_parent);
    
    // Destroy the plugin
    void (*destroy)(void* self);
    
    // Optional: Get the stream node of this plugin (to attach to a parent)
    tsa_stream_t* (*get_stream)(void* self);
    
    // Optional: Reset internal state
    void (*reset)(void* self);
    
} tsa_plugin_ops_t;

#define TSA_MAX_PLUGINS 32

void tsa_plugin_register(tsa_plugin_ops_t* ops);
tsa_plugin_ops_t* tsa_plugin_find(const char* name);

#endif // TSA_PLUGIN_H
