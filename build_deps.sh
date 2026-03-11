#!/bin/bash
set -e

# TSA Build System v3.8 (Pure Out-of-source Static Build)
# Builds dependencies in a separate directory to keep source trees pristine.

export CFLAGS="-fPIC $CFLAGS"
export CXXFLAGS="-fPIC $CXXFLAGS"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"
BUILD_TMP="$DEPS_DIR/tmp_build"

echo "=== TSA: Building Static Dependencies (Out-of-source) ==="
mkdir -p "$DEPS_DIR"
rm -rf "$BUILD_TMP" && mkdir -p "$BUILD_TMP"

# 1. Build SRT
if [ -d "$DEPS_DIR/srt_src" ]; then
    echo "--- Building SRT ---"
    mkdir -p "$BUILD_TMP/srt" && cd "$BUILD_TMP/srt"
    cmake "$DEPS_DIR/srt_src" -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/srt" \
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
             -DCMAKE_INSTALL_LIBDIR=lib \
             -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
             -DENABLE_APPS=OFF -DENABLE_TESTING=OFF
    make -j$(nproc) && make install
fi

# 2. Build libpcap
if [ -d "$DEPS_DIR/libpcap_src" ]; then
    echo "--- Building libpcap ---"
    mkdir -p "$BUILD_TMP/pcap" && cd "$BUILD_TMP/pcap"
    cmake "$DEPS_DIR/libpcap_src" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
             -DENABLE_SHARED=OFF -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/libpcap" \
             -DBUILD_WITH_LIBNL=OFF -DENABLE_DBUS=OFF -DENABLE_RDMA=OFF -DBUILD_WITH_LIBUSB=OFF
    make -j$(nproc) && make install
fi

# 3. Build Lua
if [ -d "$DEPS_DIR/lua_src" ]; then
    echo "--- Building Lua ---"
    # Lua's Makefile doesn't support out-of-source build easily,
    # so we copy to BUILD_TMP first to keep original source clean.
    cp -r "$DEPS_DIR/lua_src" "$BUILD_TMP/lua"
    cd "$BUILD_TMP/lua"
    make linux MYCFLAGS="-fPIC" -j$(nproc) || make generic MYCFLAGS="-fPIC" -j$(nproc)
    mkdir -p "$DEPS_DIR/lua/include" "$DEPS_DIR/lua/lib"
    cp src/lua.h src/luaconf.h src/lualib.h src/lauxlib.h src/lua.hpp "$DEPS_DIR/lua/include/"
    cp src/liblua.a "$DEPS_DIR/lua/lib/"
fi

# 4. Build Zlib
if [ -d "$DEPS_DIR/zlib_src" ]; then
    echo "--- Building Zlib ---"
    mkdir -p "$BUILD_TMP/zlib" && cp -r "$DEPS_DIR/zlib_src"/* "$BUILD_TMP/zlib/"
    cd "$BUILD_TMP/zlib"
    CFLAGS="-fPIC" ./configure --prefix="$DEPS_DIR/zlib" --static
    make -j$(nproc) && make install
fi

# 5. Build Libcurl
if [ -d "$DEPS_DIR/curl_src" ]; then
    echo "--- Building Curl ---"
    mkdir -p "$BUILD_TMP/curl" && cp -r "$DEPS_DIR/curl_src"/* "$BUILD_TMP/curl/"
    cd "$BUILD_TMP/curl"
    CFLAGS="-fPIC" ./configure --prefix="$DEPS_DIR/curl" \
                --enable-static --disable-shared \
                --with-zlib="$DEPS_DIR/zlib" --with-openssl \
                --disable-ftp --disable-ldap --disable-rtsp --disable-proxy \
                --without-libpsl --without-libidn2 --without-brotli
    make -j$(nproc) && make install
fi

# Cleanup
rm -rf "$BUILD_TMP"
echo "=== TSA: All Static Dependencies Built Successfully (Sources Pristine) ==="
