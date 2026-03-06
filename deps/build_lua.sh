#!/bin/bash
set -e
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"
LUA_VERSION="5.4.6"
LUA_DIR="$DEPS_DIR/lua"

echo "--- Downloading Lua $LUA_VERSION ---"
cd "$DEPS_DIR"
if [ ! -d "$LUA_DIR" ]; then
    curl -R -O http://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz
    tar -zxf lua-$LUA_VERSION.tar.gz
    mv lua-$LUA_VERSION lua
    rm lua-$LUA_VERSION.tar.gz
fi

echo "--- Building Lua (Static) ---"
cd "$LUA_DIR"
make linux
