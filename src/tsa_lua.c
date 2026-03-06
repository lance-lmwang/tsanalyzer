#include "tsa_lua.h"
#include "tsa_plugin.h"
#include "tsa_internal.h"
#include "tsa_source.h"
#include "tsp.h"
#include <stdlib.h>
#include <string.h>

#define TSA_LUA_SOURCE_MT "tsa.source"
#define TSA_LUA_OUTPUT_MT "tsa.output"

struct tsa_lua_s {
    lua_State* L;
    tsa_handle_t* tsa;
};

// --- Input Source Binding ---
typedef struct {
    tsa_source_t* src;
} lua_tsa_source_t;

static void dummy_on_packets(void* user_data, const uint8_t* pkts, int count, uint64_t now_ns) {
    (void)user_data; (void)pkts; (void)count; (void)now_ns;
}
static void dummy_on_status(void* user_data, int status_code, const char* msg) {
    (void)user_data; (void)status_code; (void)msg;
}

static int l_tsa_udp_input(lua_State* L) {
    int port = luaL_checkinteger(L, 1);

    lua_tsa_source_t* obj = (lua_tsa_source_t*)lua_newuserdata(L, sizeof(lua_tsa_source_t));
    luaL_getmetatable(L, TSA_LUA_SOURCE_MT);
    lua_setmetatable(L, -2);

    // We will hook real callbacks when link (set_upstream) happens in Step 3
    tsa_source_callbacks_t cbs = { dummy_on_packets, dummy_on_status };
    obj->src = tsa_source_create(TSA_SOURCE_UDP, NULL, NULL, port, &cbs, NULL);
    if (!obj->src) {
        return luaL_error(L, "Failed to create UDP input");
    }

    tsa_source_start(obj->src);
    return 1;
}

static int l_tsa_source_gc(lua_State* L) {
    lua_tsa_source_t* obj = (lua_tsa_source_t*)luaL_checkudata(L, 1, TSA_LUA_SOURCE_MT);
    if (obj->src) {
        tsa_source_stop(obj->src);
        tsa_source_destroy(obj->src);
        obj->src = NULL;
    }
    return 0;
}

// --- Output Destination Binding ---
typedef struct {
    tsp_handle_t* tsp;
} lua_tsa_output_t;

static int l_tsa_udp_output(lua_State* L) {
    const char* ip = luaL_checkstring(L, 1);
    int port = luaL_checkinteger(L, 2);

    lua_tsa_output_t* obj = (lua_tsa_output_t*)lua_newuserdata(L, sizeof(lua_tsa_output_t));
    luaL_getmetatable(L, TSA_LUA_OUTPUT_MT);
    lua_setmetatable(L, -2);

    tsp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.dest_ip = ip;
    cfg.port = port;
    cfg.mode = TSPACER_MODE_BASIC;
    cfg.bitrate = 10000000; // 10Mbps default

    obj->tsp = tsp_create(&cfg);
    if (!obj->tsp) {
        return luaL_error(L, "Failed to create UDP output");
    }

    tsp_start(obj->tsp);
    return 1;
}

static int l_tsa_output_gc(lua_State* L) {
    lua_tsa_output_t* obj = (lua_tsa_output_t*)luaL_checkudata(L, 1, TSA_LUA_OUTPUT_MT);
    if (obj->tsp) {
        tsp_stop(obj->tsp);
        tsp_destroy(obj->tsp);
        obj->tsp = NULL;
    }
    return 0;
}

// --- General API ---

static int l_tsa_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    printf("[Lua] %s\n", msg);
    return 0;
}

// tsa.add_plugin("TR101290_CORE")
static int l_tsa_add_plugin(lua_State* L) {
    tsa_lua_t* lua = (tsa_lua_t*)lua_touserdata(L, lua_upvalueindex(1));
    const char* name = luaL_checkstring(L, 1);

    tsa_plugin_ops_t* ops = tsa_plugin_find(name);
    if (!ops) {
        return luaL_error(L, "Plugin not found: %s", name);
    }

    tsa_plugin_attach_instance(lua->tsa, ops);
    printf("[Lua] Attached plugin: %s\n", name);
    return 0;
}

static const struct luaL_Reg tsa_lib[] = {
    {"log", l_tsa_log},
    {"add_plugin", l_tsa_add_plugin},
    {"udp_input", l_tsa_udp_input},
    {"udp_output", l_tsa_udp_output},
    {NULL, NULL}
};

static void register_metatables(lua_State* L) {
    luaL_newmetatable(L, TSA_LUA_SOURCE_MT);
    lua_pushcfunction(L, l_tsa_source_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    luaL_newmetatable(L, TSA_LUA_OUTPUT_MT);
    lua_pushcfunction(L, l_tsa_output_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
}

tsa_lua_t* tsa_lua_create(tsa_handle_t* tsa) {
    tsa_lua_t* lua = calloc(1, sizeof(tsa_lua_t));
    lua->tsa = tsa;
    lua->L = luaL_newstate();
    luaL_openlibs(lua->L);

    register_metatables(lua->L);

    // Register TSA library
    lua_newtable(lua->L);
    lua_pushlightuserdata(lua->L, lua);
    luaL_setfuncs(lua->L, tsa_lib, 1);
    lua_setglobal(lua->L, "tsa");

    return lua;
}

void tsa_lua_destroy(tsa_lua_t* lua) {
    if (!lua) return;
    if (lua->L) lua_close(lua->L);
    free(lua);
}

int tsa_lua_run_script(tsa_lua_t* lua, const char* script) {
    if (luaL_dostring(lua->L, script) != LUA_OK) {
        fprintf(stderr, "Lua Error: %s\n", lua_tostring(lua->L, -1));
        return -1;
    }
    return 0;
}

int tsa_lua_run_file(tsa_lua_t* lua, const char* filename) {
    if (luaL_dofile(lua->L, filename) != LUA_OK) {
        fprintf(stderr, "Lua Error: %s\n", lua_tostring(lua->L, -1));
        return -1;
    }
    return 0;
}