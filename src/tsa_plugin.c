#include "tsa_plugin.h"
#include <string.h>

static tsa_plugin_ops_t* s_plugins[TSA_MAX_PLUGINS];
static int s_plugin_count = 0;

void tsa_plugin_register(tsa_plugin_ops_t* ops) {
    if (s_plugin_count < TSA_MAX_PLUGINS && ops) {
        s_plugins[s_plugin_count++] = ops;
    }
}

tsa_plugin_ops_t* tsa_plugin_find(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < s_plugin_count; ++i) {
        if (strcmp(s_plugins[i]->name, name) == 0) {
            return s_plugins[i];
        }
    }
    return NULL;
}
