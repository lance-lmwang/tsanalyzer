#!/bin/bash
set -e

# TSA Build System v3.7 (Clean Static + PIC)
# Build all third-party dependencies as static libraries with -fPIC.
# This version automatically cleans up build artifacts to keep submodules pure.

export CFLAGS="-fPIC $CFLAGS"
export CXXFLAGS="-fPIC $CXXFLAGS"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"

echo "=== TSA: Building Production Static Dependencies ==="
mkdir -p "$DEPS_DIR"

# 1. Build SRT
if [ -d "$DEPS_DIR/srt_src" ]; then
    echo "--- Building SRT ---"
    cd "$DEPS_DIR/srt_src"
    rm -rf build && mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/srt" \
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
             -DCMAKE_INSTALL_LIBDIR=lib \
             -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
             -DENABLE_APPS=OFF -DENABLE_TESTING=OFF
    make -j$(nproc) && make install
    cd .. && rm -rf build
fi

# 2. Build libpcap (Minimalist)
if [ -d "$DEPS_DIR/libpcap_src" ]; then
    echo "--- Building libpcap ---"
    cd "$DEPS_DIR/libpcap_src"
    rm -rf build && mkdir build && cd build
    cmake .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
             -DENABLE_SHARED=OFF -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/libpcap" \
             -DBUILD_WITH_LIBNL=OFF -DENABLE_DBUS=OFF -DENABLE_RDMA=OFF -DBUILD_WITH_LIBUSB=OFF
    make -j$(nproc) && make install
    cd .. && rm -rf build
fi

# 3. Build Lua
if [ -d "$DEPS_DIR/lua_src" ]; then
    echo "--- Building Lua ---"
    cd "$DEPS_DIR/lua_src"
    # Clean previous build artifacts
    make clean || true
    make linux MYCFLAGS="-fPIC" -j$(nproc) || make generic MYCFLAGS="-fPIC" -j$(nproc)
    mkdir -p "$DEPS_DIR/lua/include" "$DEPS_DIR/lua/lib"
    cp src/lua.h src/luaconf.h src/lualib.h src/lauxlib.h src/lua.hpp "$DEPS_DIR/lua/include/"
    cp src/liblua.a "$DEPS_DIR/lua/lib/"
    # Lua's 'make clean' is usually sufficient
    make clean
fi

# 4. Build Zlib
if [ -d "$DEPS_DIR/zlib_src" ]; then
    echo "--- Building Zlib ---"
    cd "$DEPS_DIR/zlib_src"
    [ -f Makefile ] && make distclean || true
    CFLAGS="-fPIC" ./configure --prefix="$DEPS_DIR/zlib" --static
    make -j$(nproc) && make install
    make distclean || true
fi

# 5. Build Libcurl
if [ -d "$DEPS_DIR/curl_src" ]; then
    echo "--- Building Curl ---"
    cd "$DEPS_DIR/curl_src"
    [ -f Makefile ] && make distclean || true
    CFLAGS="-fPIC" ./configure --prefix="$DEPS_DIR/curl" \
                --enable-static --disable-shared \
                --with-zlib="$DEPS_DIR/zlib" --with-openssl \
                --disable-ftp --disable-ldap --disable-rtsp --disable-proxy \
                --without-libpsl --without-libidn2 --without-brotli
    make -j$(nproc) && make install
    make distclean || true
fi

echo "=== TSA: Clean Static Dependencies Built & Workspace Purged ==="
