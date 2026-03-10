#include "tsa_plugin.h"

#include <string.h>

#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "PLUGIN_MGR"

/* Static Registry for all available plugins */
static tsa_plugin_ops_t* s_registry[MAX_TSA_PLUGINS];
static int s_registry_count = 0;

/* External ops from individual plugin files - Phase 2.2 */
extern tsa_plugin_ops_t tsa_scte35_engine;
extern tsa_plugin_ops_t tr101290_ops;
extern tsa_plugin_ops_t pcr_ops;
extern tsa_plugin_ops_t essence_plugin_ops;

void tsa_plugin_register(tsa_plugin_ops_t* ops) {
    if (!ops || s_registry_count >= MAX_TSA_PLUGINS) {
        tsa_error(TAG, "Failed to register plugin: registry full or invalid ops");
        return;
    }

    /* Avoid double registration */
    for (int i = 0; i < s_registry_count; i++) {
        if (s_registry[i] == ops) return;
    }

    s_registry[s_registry_count++] = ops;
    tsa_debug(TAG, "Plugin [%s] registered in registry", ops->name);
}

tsa_plugin_ops_t* tsa_plugin_find(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < s_registry_count; i++) {
        if (s_registry[i] && s_registry[i]->name && strcmp(s_registry[i]->name, name) == 0) {
            return s_registry[i];
        }
    }
    return NULL;
}

/**
 * Initialize the global plugin registry with all builtin engines.
 */
void tsa_plugins_init_registry(void) {
    s_registry_count = 0;
    tsa_plugin_register(&tsa_scte35_engine);
    tsa_plugin_register(&tr101290_ops);
    tsa_plugin_register(&pcr_ops);
    tsa_plugin_register(&essence_plugin_ops);
    tsa_info(TAG, "Global plugin registry initialized with %d engines", s_registry_count);
}

/**
 * Attach all registered builtin plugins to the handle.
 */
void tsa_plugins_attach_builtin(tsa_handle_t* h) {
    if (!h) return;
    tsa_info(TAG, "Attaching %d builtin plugins to handle %p", s_registry_count, h);
    for (int i = 0; i < s_registry_count; i++) {
        tsa_plugin_attach_instance(h, s_registry[i]);
    }
}

/**
 * Instantiate a plugin using the handle's pre-allocated context buffer (In-Place Init).
 */
void tsa_plugin_attach_instance(tsa_handle_t* h, tsa_plugin_ops_t* ops) {
    if (!h || !ops) return;

    if (h->plugin_count >= MAX_TSA_PLUGINS) {
        tsa_error(TAG, "Cannot attach plugin [%s]: maximum plugins reached (%d)", ops->name, MAX_TSA_PLUGINS);
        return;
    }

    /* Find an empty slot in the handle's plugins array */
    int slot = -1;
    for (int i = 0; i < MAX_TSA_PLUGINS; i++) {
        if (!h->plugins[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        tsa_error(TAG, "INTERNAL ERROR: plugin_count < MAX but no free slot for [%s]", ops->name);
        return;
    }

    tsa_debug(TAG, "Instantiating [%s] in slot %d (Zero-Allocation)", ops->name, slot);

    /* Zero out the context buffer before creation */
    memset(h->plugins[slot].context, 0, MAX_PLUGIN_CONTEXT_SIZE);

    /* Phase 2.3: In-Place Initialization using handle's pre-allocated buffer */
    void* instance = ops->create(h, h->plugins[slot].context);
    if (!instance) {
        tsa_error(TAG, "Failed to create plugin instance for [%s]", ops->name);
        return;
    }

    h->plugins[slot].ops = ops;
    h->plugins[slot].instance = instance;
    h->plugins[slot].in_use = true;

    /* Legacy Support: Stream tree attachment (to be removed in Phase 3) */
    if (ops->get_stream) {
        tsa_stream_t* child = ops->get_stream(instance);
        if (child) {
            tsa_stream_attach(&h->root_stream, child);
        }
    }

    h->plugin_count++;
    tsa_info(TAG, "Plugin [%s] attached to handle successfully", ops->name);
}

/**
 * Unified destruction of all attached plugins.
 * Note: Only calls destroy callback; memory is managed by the handle.
 */
void tsa_plugins_destroy_all(tsa_handle_t* h) {
    if (!h) return;
    tsa_info(TAG, "Destroying all plugins for handle %p", h);

    for (int i = 0; i < MAX_TSA_PLUGINS; i++) {
        if (h->plugins[i].in_use) {
            if (h->plugins[i].ops && h->plugins[i].ops->destroy) {
                h->plugins[i].ops->destroy(h->plugins[i].instance);
            }
            h->plugins[i].in_use = false;
            h->plugins[i].instance = NULL;
            h->plugins[i].ops = NULL;
        }
    }
    h->plugin_count = 0;
}
