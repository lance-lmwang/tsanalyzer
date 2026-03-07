#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsa_lua.h"

void test_lua_basic() {
    printf("Testing Lua Integration...\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    tsa_lua_t* lua = tsa_lua_create(h);
    assert(lua != NULL);

    // Initial plugin count from tsa_create (it attaches defaults)
    int initial_count = h->plugin_count;
    printf("Initial plugin count: %d\n", initial_count);

    // Run a script to log and add a plugin
    const char* script =
        "tsa.log('Hello from Lua!')\n"
        "tsa.add_plugin('TR101290_CORE')\n";

    assert(tsa_lua_run_script(lua, script) == 0);

    // Verify plugin count increased
    assert(h->plugin_count == initial_count + 1);
    assert(strcmp(h->plugins[h->plugin_count - 1].ops->name, "TR101290_CORE") == 0);

    tsa_lua_destroy(lua);
    tsa_destroy(h);

    printf("Lua Integration OK.\n");
}

int main() {
    test_lua_basic();
    return 0;
}