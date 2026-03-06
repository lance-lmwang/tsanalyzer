#include "tsa_lua.h"
#include "tsa_plugin.h"
#include "tsa_internal.h"
#include <stdlib.h>
#include <string.h>

struct tsa_lua_s {
    lua_State* L;
    tsa_handle_t* tsa;
};

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
    {NULL, NULL}
};

tsa_lua_t* tsa_lua_create(tsa_handle_t* tsa) {
    tsa_lua_t* lua = calloc(1, sizeof(tsa_lua_t));
    lua->tsa = tsa;
    lua->L = luaL_newstate();
    luaL_openlibs(lua->L);

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