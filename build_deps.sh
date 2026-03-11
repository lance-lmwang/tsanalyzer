#!/bin/bash
set -e

# TSA Build System v3.5 (Production Static + fPIC)
# This script builds all third-party dependencies as static libraries with -fPIC.
export CFLAGS="-fPIC $CFLAGS"
export CXXFLAGS="-fPIC $CXXFLAGS"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"

echo "=== TSA: Building Static Dependencies with PIC Support ==="
mkdir -p "$DEPS_DIR"

# 1. Build SRT (Source -> Static .a)
if [ -d "$DEPS_DIR/srt_src" ]; then
    echo "--- Building SRT (Source) ---"
    rm -rf "$DEPS_DIR/srt" && mkdir -p "$DEPS_DIR/srt"
    cd "$DEPS_DIR/srt_src"
    rm -rf build && mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/srt" \
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
             -DCMAKE_INSTALL_LIBDIR=lib \
             -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
             -DENABLE_APPS=OFF -DENABLE_TESTING=OFF
    make -j$(nproc) && make install
fi

# 2. Build libpcap (Source -> Static .a)
if [ -d "$DEPS_DIR/libpcap_src" ]; then
    echo "--- Building libpcap (Source) ---"
    rm -rf "$DEPS_DIR/libpcap" && mkdir -p "$DEPS_DIR/libpcap"
    cd "$DEPS_DIR/libpcap_src"
    rm -rf build && mkdir build && cd build
    cmake .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
             -DENABLE_SHARED=OFF -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/libpcap" \
             -DBUILD_WITH_LIBNL=OFF -DENABLE_DBUS=OFF -DENABLE_RDMA=OFF
    make -j$(nproc) && make install
fi

# 3. Build Lua (Source -> Static .a)
if [ -d "$DEPS_DIR/lua_src" ]; then
    echo "--- Building Lua (Source) ---"
    rm -rf "$DEPS_DIR/lua" && mkdir -p "$DEPS_DIR/lua"
    cd "$DEPS_DIR/lua_src"
    make linux MYCFLAGS="-fPIC" -j$(nproc) || make generic MYCFLAGS="-fPIC" -j$(nproc)
    mkdir -p "$DEPS_DIR/lua/include" "$DEPS_DIR/lua/lib"
    cp src/lua.h src/luaconf.h src/lualib.h src/lauxlib.h src/lua.hpp "$DEPS_DIR/lua/include/"
    cp src/liblua.a "$DEPS_DIR/lua/lib/"
fi

# 4. Build Zlib (Source -> Static .a)
if [ -d "$DEPS_DIR/zlib_src" ]; then
    echo "--- Building Zlib (Source) ---"
    rm -rf "$DEPS_DIR/zlib" && mkdir -p "$DEPS_DIR/zlib"
    cd "$DEPS_DIR/zlib_src"
    [ -f Makefile ] && make distclean || true
    CFLAGS="-fPIC" ./configure --prefix="$DEPS_DIR/zlib" --static
    make -j$(nproc) && make install
fi

# 5. Build Libcurl (Source -> Static .a)
if [ -d "$DEPS_DIR/curl_src" ]; then
    echo "--- Building Curl (Source) ---"
    rm -rf "$DEPS_DIR/curl" && mkdir -p "$DEPS_DIR/curl"
    cd "$DEPS_DIR/curl_src"
    [ -f Makefile ] && make distclean || true
    # Link to the zlib we just built
    CFLAGS="-fPIC" ./configure --prefix="$DEPS_DIR/curl" \
                --enable-static --disable-shared \
                --with-zlib="$DEPS_DIR/zlib" --with-openssl \
                --disable-ftp --disable-ldap --disable-rtsp --disable-proxy \
                --without-libpsl --without-libidn2 --without-brotli
    make -j$(nproc) && make install
fi

echo "=== TSA: All Static Dependencies Built from Source Successfully ==="
