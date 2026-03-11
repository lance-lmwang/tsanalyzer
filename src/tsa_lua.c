#include "tsa_lua.h"

#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_plugin.h"
#include "tsa_source.h"
#include "tsp.h"

#define TSA_LUA_SOURCE_MT "tsa.source"
#define TSA_LUA_OUTPUT_MT "tsa.output"
#define TSA_LUA_ANALYZER_MT "tsa.analyzer"

struct tsa_lua_s {
    lua_State* L;
    tsa_handle_t* tsa;
};

// --- Definitions ---
typedef struct lua_tsa_source lua_tsa_source_t;
typedef struct lua_tsa_output lua_tsa_output_t;
typedef struct lua_tsa_analyzer lua_tsa_analyzer_t;

struct lua_tsa_analyzer {
    tsa_handle_t* h;
    bool is_owned;  // true if created by Lua, false if passed from global
};

struct lua_tsa_output {
    tsp_handle_t* tsp;
};

#define MAX_CONSUMERS 8
typedef enum { CONSUMER_ANALYZER, CONSUMER_OUTPUT } consumer_type_t;

typedef struct {
    consumer_type_t type;
    void* ptr;
} source_consumer_t;

struct lua_tsa_source {
    tsa_source_t* src;
    source_consumer_t consumers[MAX_CONSUMERS];
    int consumer_count;
};

// --- Router Callbacks ---
static void router_on_packets(void* user_data, const uint8_t* pkts, int count, uint64_t now_ns) {
    lua_tsa_source_t* obj = (lua_tsa_source_t*)user_data;
    for (int i = 0; i < obj->consumer_count; i++) {
        if (obj->consumers[i].type == CONSUMER_ANALYZER) {
            tsa_handle_t* h = (tsa_handle_t*)obj->consumers[i].ptr;
            for (int p = 0; p < count; p++) {
                tsa_process_packet(h, pkts + (p * 188), now_ns);
            }
        } else if (obj->consumers[i].type == CONSUMER_OUTPUT) {
            tsp_handle_t* tsp = (tsp_handle_t*)obj->consumers[i].ptr;
            tsp_enqueue(tsp, pkts, count);
        }
    }
}

static void router_on_status(void* user_data, int status_code, const char* msg) {
    (void)user_data;
    (void)status_code;
    (void)msg;
}

// --- Source Methods ---
static int l_tsa_udp_input(lua_State* L) {
    int port = luaL_checkinteger(L, 1);

    lua_tsa_source_t* obj = (lua_tsa_source_t*)lua_newuserdata(L, sizeof(lua_tsa_source_t));
    memset(obj, 0, sizeof(lua_tsa_source_t));
    luaL_getmetatable(L, TSA_LUA_SOURCE_MT);
    lua_setmetatable(L, -2);

    tsa_source_callbacks_t cbs = {router_on_packets, router_on_status, NULL};
    obj->src = tsa_source_create(TSA_SOURCE_UDP, NULL, NULL, port, &cbs, obj);
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

// --- Output Methods ---
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
    cfg.bitrate = 10000000;

    obj->tsp = tsp_create(&cfg);
    if (!obj->tsp) {
        return luaL_error(L, "Failed to create UDP output");
    }

    tsp_start(obj->tsp);
    return 1;
}

static int l_tsa_output_set_upstream(lua_State* L) {
    lua_tsa_output_t* out = (lua_tsa_output_t*)luaL_checkudata(L, 1, TSA_LUA_OUTPUT_MT);
    lua_tsa_source_t* src = (lua_tsa_source_t*)luaL_checkudata(L, 2, TSA_LUA_SOURCE_MT);

    if (src->consumer_count < MAX_CONSUMERS) {
        src->consumers[src->consumer_count].type = CONSUMER_OUTPUT;
        src->consumers[src->consumer_count].ptr = out->tsp;
        src->consumer_count++;
    }
    return 0;
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

// --- Analyzer Methods ---
static int l_tsa_analyzer(lua_State* L) {
    lua_tsa_analyzer_t* obj = (lua_tsa_analyzer_t*)lua_newuserdata(L, sizeof(lua_tsa_analyzer_t));
    luaL_getmetatable(L, TSA_LUA_ANALYZER_MT);
    lua_setmetatable(L, -2);

    tsa_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.op_mode = TSA_MODE_LIVE;

    obj->h = tsa_create(&cfg);
    obj->is_owned = true;

    if (!obj->h) {
        return luaL_error(L, "Failed to create analyzer");
    }
    return 1;
}

static int l_tsa_analyzer_set_upstream(lua_State* L) {
    lua_tsa_analyzer_t* ana = (lua_tsa_analyzer_t*)luaL_checkudata(L, 1, TSA_LUA_ANALYZER_MT);
    lua_tsa_source_t* src = (lua_tsa_source_t*)luaL_checkudata(L, 2, TSA_LUA_SOURCE_MT);

    if (src->consumer_count < MAX_CONSUMERS) {
        src->consumers[src->consumer_count].type = CONSUMER_ANALYZER;
        src->consumers[src->consumer_count].ptr = ana->h;
        src->consumer_count++;
    }
    return 0;
}

static int l_tsa_analyzer_join_pid(lua_State* L) {
    lua_tsa_analyzer_t* ana = (lua_tsa_analyzer_t*)luaL_checkudata(L, 1, TSA_LUA_ANALYZER_MT);
    int pid = luaL_checkinteger(L, 2);
    if (pid >= 0 && pid < 8192) {
        ana->h->pid_filtering_enabled = true;
        ana->h->pid_allowed[pid] = true;
    }
    return 0;
}

static int l_tsa_analyzer_drop_pid(lua_State* L) {
    lua_tsa_analyzer_t* ana = (lua_tsa_analyzer_t*)luaL_checkudata(L, 1, TSA_LUA_ANALYZER_MT);
    int pid = luaL_checkinteger(L, 2);
    if (pid >= 0 && pid < 8192) {
        ana->h->pid_filtering_enabled = true;
        ana->h->pid_allowed[pid] = false;
    }
    return 0;
}

static int l_tsa_analyzer_on(lua_State* L) {
    lua_tsa_analyzer_t* ana = (lua_tsa_analyzer_t*)luaL_checkudata(L, 1, TSA_LUA_ANALYZER_MT);
    const char* event_name = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    // Save callback in registry: registry["tsa_cb_" .. event_name] = function
    char key[64];
    snprintf(key, sizeof(key), "tsa_cb_%s", event_name);

    lua_pushvalue(L, 3);                      // Copy function to top of stack
    lua_setfield(L, LUA_REGISTRYINDEX, key);  // registry[key] = function

    return 0;
}

static int l_tsa_analyzer_gc(lua_State* L) {
    lua_tsa_analyzer_t* obj = (lua_tsa_analyzer_t*)luaL_checkudata(L, 1, TSA_LUA_ANALYZER_MT);
    if (obj->h && obj->is_owned) {
        tsa_destroy(obj->h);
        obj->h = NULL;
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

static const struct luaL_Reg tsa_lib[] = {{"log", l_tsa_log},
                                          {"add_plugin", l_tsa_add_plugin},
                                          {"udp_input", l_tsa_udp_input},
                                          {"udp_output", l_tsa_udp_output},
                                          {"analyzer", l_tsa_analyzer},
                                          {NULL, NULL}};

static const struct luaL_Reg output_methods[] = {{"set_upstream", l_tsa_output_set_upstream}, {NULL, NULL}};

static const struct luaL_Reg analyzer_methods[] = {{"set_upstream", l_tsa_analyzer_set_upstream},
                                                   {"join_pid", l_tsa_analyzer_join_pid},
                                                   {"drop_pid", l_tsa_analyzer_drop_pid},
                                                   {"on", l_tsa_analyzer_on},
                                                   {NULL, NULL}};

static void register_metatables(lua_State* L) {
    luaL_newmetatable(L, TSA_LUA_SOURCE_MT);
    lua_pushcfunction(L, l_tsa_source_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    luaL_newmetatable(L, TSA_LUA_OUTPUT_MT);
    lua_pushcfunction(L, l_tsa_output_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, output_methods, 0);
    lua_pop(L, 1);

    luaL_newmetatable(L, TSA_LUA_ANALYZER_MT);
    lua_pushcfunction(L, l_tsa_analyzer_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, analyzer_methods, 0);
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

int tsa_lua_process_section(tsa_lua_t* lua, uint16_t pid, uint8_t table_id, const uint8_t* payload, size_t len) {
    if (!lua || !lua->L) return -1;
    lua_State* L = lua->L;

    // Check if the global function 'on_ts_section' exists
    lua_getglobal(L, "on_ts_section");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;  // No handler installed, simply return
    }

    lua_pushinteger(L, pid);
    lua_pushinteger(L, table_id);
    lua_pushlstring(L, (const char*)payload, len);

    // Call function with 3 arguments and 0 returns
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        fprintf(stderr, "Lua Runtime Error in on_ts_section: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }

    return 0;
}

int tsa_lua_push_event(tsa_lua_t* lua, const char* event_name, uint16_t pid, const char* message) {
    if (!lua || !lua->L) return -1;
    lua_State* L = lua->L;

    char key[64];
    snprintf(key, sizeof(key), "tsa_cb_%s", event_name);

    lua_getfield(L, LUA_REGISTRYINDEX, key);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;  // No callback registered
    }

    lua_newtable(L);
    lua_pushstring(L, event_name);
    lua_setfield(L, -2, "event");
    lua_pushinteger(L, pid);
    lua_setfield(L, -2, "pid");
    lua_pushstring(L, message);
    lua_setfield(L, -2, "message");

    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        fprintf(stderr, "Lua Error in event callback '%s': %s\n", event_name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }

    return 0;
}

bool tsa_lua_get_global_bool(tsa_lua_t* lua, const char* name) {
    if (!lua || !lua->L) return false;
    lua_getglobal(lua->L, name);
    bool val = lua_toboolean(lua->L, -1);
    lua_pop(lua->L, 1);
    return val;
}