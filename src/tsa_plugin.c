#include "tsa_plugin.h"

#include <string.h>

#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "PLUGIN_MGR"

static tsa_plugin_ops_t* s_registry[MAX_TSA_PLUGINS];
static int s_registry_count = 0;

/* External ops from individual plugin files */
extern tsa_plugin_ops_t tsa_scte35_engine;
extern tsa_plugin_ops_t tr101290_ops;
extern tsa_plugin_ops_t pcr_ops;
extern tsa_plugin_ops_t essence_plugin_ops;

void tsa_plugin_register(tsa_plugin_ops_t* ops) {
    if (!ops || s_registry_count >= MAX_TSA_PLUGINS) return;

    // Avoid double registration
    for (int i = 0; i < s_registry_count; i++) {
        if (s_registry[i] == ops) return;
    }

    s_registry[s_registry_count++] = ops;
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

void tsa_plugins_init_registry(void) {
    s_registry_count = 0;
    tsa_plugin_register(&tsa_scte35_engine);
    tsa_plugin_register(&tr101290_ops);
    tsa_plugin_register(&pcr_ops);
    tsa_plugin_register(&essence_plugin_ops);
}

void tsa_plugins_attach_builtin(tsa_handle_t* h) {
    tsa_info(TAG, "Attaching %d builtin plugins to handle %p", s_registry_count, h);
    for (int i = 0; i < s_registry_count; i++) {
        tsa_plugin_attach_instance(h, s_registry[i]);
    }
}

void tsa_plugin_attach_instance(tsa_handle_t* h, tsa_plugin_ops_t* ops) {
    if (!h || !ops || h->plugin_count >= MAX_TSA_PLUGINS) return;

    int slot = -1;
    for (int i = 0; i < MAX_TSA_PLUGINS; i++) {
        if (!h->plugins[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        tsa_error(TAG, "No free slots for plugin [%s]", ops->name);
        return;
    }

    tsa_debug(TAG, "Creating instance for [%s] in slot %d", ops->name, slot);

    void* instance = ops->create(h, h->plugins[slot].context);
    if (!instance) {
        tsa_error(TAG, "Failed to create plugin [%s]", ops->name);
        return;
    }

    h->plugins[slot].ops = ops;
    h->plugins[slot].instance = instance;
    h->plugins[slot].in_use = true;

    if (ops->get_stream) {
        tsa_stream_t* child = ops->get_stream(instance);
        if (child) {
            tsa_stream_attach(&h->root_stream, child);
        }
    }

    h->plugin_count++;
    tsa_info(TAG, "Plugin [%s] attached", ops->name);
}

void tsa_plugins_destroy_all(tsa_handle_t* h) {
    if (!h) return;
    for (int i = 0; i < MAX_TSA_PLUGINS; i++) {
        if (h->plugins[i].in_use && h->plugins[i].ops->destroy) {
            h->plugins[i].ops->destroy(h->plugins[i].instance);
            h->plugins[i].in_use = false;
        }
    }
    h->plugin_count = 0;
}
