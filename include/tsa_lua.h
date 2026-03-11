#ifndef TSA_LUA_H
#define TSA_LUA_H

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "tsa.h"

typedef struct tsa_lua_s tsa_lua_t;

tsa_lua_t* tsa_lua_create(tsa_handle_t* tsa);
void tsa_lua_destroy(tsa_lua_t* lua);

int tsa_lua_run_script(tsa_lua_t* lua, const char* script);
int tsa_lua_run_file(tsa_lua_t* lua, const char* filename);
int tsa_lua_process_section(tsa_lua_t* lua, uint16_t pid, uint8_t table_id, const uint8_t* payload, size_t len);

#endif  // TSA_LUA_H