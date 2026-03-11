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
        "tsa.log('Pipeline created successfully with PID filtering!')\n";

    int ret = tsa_lua_run_script(lua, script);
    (void)ret;
    assert(ret == 0);

    tsa_lua_destroy(lua);
    tsa_destroy(h);
}

int main() {
    test_lua_pipeline();
    printf("LUA PIPELINE TEST PASSED!\n");
    return 0;
}