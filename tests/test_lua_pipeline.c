#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"
#include "tsa_lua.h"

void test_lua_pipeline() {
    printf("Testing Lua Pipeline...\n");

    tsa_handle_t* h = tsa_create(NULL);
    assert(h != NULL);

    tsa_lua_t* lua = tsa_lua_create(h);
    assert(lua != NULL);

    const char* script =
        "local input = tsa.udp_input(5000)\n"
        "local analyzer = tsa.analyzer()\n"
        "local output = tsa.udp_output('127.0.0.1', 6000)\n"
        "analyzer:set_upstream(input)\n"
        "output:set_upstream(input)\n"
        "analyzer:join_pid(0x100)\n"
        "analyzer:drop_pid(0x200)\n"
        "tsa_event_fired = false\n"
        "analyzer:on('SYNC', function(evt)\n"
        "    tsa.log('Lua received event: ' .. evt.event .. ' on PID ' .. tostring(evt.pid))\n"
        "    tsa_event_fired = true\n"
        "end)\n"
        "tsa.log('Pipeline created successfully with PID filtering and Event Callbacks!')\n";

    int ret = tsa_lua_run_script(lua, script);
    (void)ret;
    assert(ret == 0);

    /* Trigger SYNC_LOSS by sending bad data to the analyzer */
    uint8_t bad_data[188];
    memset(bad_data, 0xFF, sizeof(bad_data));
    for (int i = 0; i < 5; i++) {
        tsa_process_packet(h, bad_data, h->stc_ns + 1000);
    }

    // Verify that the Lua callback was executed
    bool fired = tsa_lua_get_global_bool(lua, "tsa_event_fired");
    (void)fired;

    assert(fired == true);
    printf("  Lua callback verified.\n");

    tsa_lua_destroy(lua);
    tsa_destroy(h);
}

int main() {
    test_lua_pipeline();
    printf("LUA PIPELINE TEST PASSED!\n");
    return 0;
}