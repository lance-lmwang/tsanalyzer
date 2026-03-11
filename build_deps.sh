#!/bin/bash
set -e

# Force absolute paths for toolchain on CentOS 7
export PATH="/opt/rh/devtoolset-9/root/usr/bin:/usr/bin:/usr/local/bin:$PATH"

# TSA Build System v3.3 (Guaranteed paths)
export CFLAGS="-fPIC $CFLAGS"
export CXXFLAGS="-fPIC $CXXFLAGS"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"

# Explicitly use cmake3 on CentOS 7
if [ -f "/usr/bin/cmake3" ]; then
    CMAKE_CMD="/usr/bin/cmake3"
else
    CMAKE_CMD="cmake"
fi

echo "=== TSA: Building Dependencies with $CMAKE_CMD ==="
mkdir -p "$DEPS_DIR"

clean_dep() { rm -rf "$DEPS_DIR/$1"; }

# 1. Build SRT
if [ -f "$DEPS_DIR/srt_src/CMakeLists.txt" ]; then
    echo "--- Building SRT ---"
    clean_dep "srt"; mkdir -p "$DEPS_DIR/srt"
    cd "$DEPS_DIR/srt_src"; rm -rf build; mkdir build; cd build
    $CMAKE_CMD .. -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/srt" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
             -DCMAKE_INSTALL_LIBDIR=lib -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
             -DENABLE_APPS=OFF -DENABLE_TESTING=OFF
    make -j$(nproc) && make install
fi

# 2. Build libpcap
if [ -f "$DEPS_DIR/libpcap_src/CMakeLists.txt" ]; then
    echo "--- Building libpcap ---"
    clean_dep "libpcap"; mkdir -p "$DEPS_DIR/libpcap"
    cd "$DEPS_DIR/libpcap_src"; rm -rf build; mkdir build; cd build
    $CMAKE_CMD .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DENABLE_SHARED=OFF \
             -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/libpcap" -DBUILD_WITH_LIBNL=OFF \
             -DENABLE_DBUS=OFF -DENABLE_RDMA=OFF
    make -j$(nproc) && make install
fi

# 3. Build Lua
if [ -f "$DEPS_DIR/lua_src/Makefile" ]; then
    echo "--- Building Lua ---"
    clean_dep "lua"
    cd "$DEPS_DIR/lua_src"
    make linux MYCFLAGS="-fPIC" -j$(nproc) || make generic MYCFLAGS="-fPIC" -j$(nproc)
    mkdir -p "$DEPS_DIR/lua/include" "$DEPS_DIR/lua/lib"
    cp src/lua.h src/luaconf.h src/lualib.h src/lauxlib.h src/lua.hpp "$DEPS_DIR/lua/include/"
    cp src/liblua.a "$DEPS_DIR/lua/lib/"
fi

# 4. Build Zlib
if [ -f "$DEPS_DIR/zlib_src/configure" ]; then
    echo "--- Building Zlib ---"
    clean_dep "zlib"; mkdir -p "$DEPS_DIR/zlib"
    cd "$DEPS_DIR/zlib_src"; [ -f Makefile ] && make distclean || true
    CFLAGS="-fPIC" ./configure --prefix="$DEPS_DIR/zlib" --static
    make -j$(nproc) && make install
fi

# 5. Build Libcurl
if [ -f "$DEPS_DIR/curl_src/configure" ]; then
    echo "--- Building Curl ---"
    clean_dep "curl"; mkdir -p "$DEPS_DIR/curl"
    cd "$DEPS_DIR/curl_src"; [ -f Makefile ] && make distclean || true
    CFLAGS="-fPIC" ./configure --prefix="$DEPS_DIR/curl" --enable-static --disable-shared \
                --with-zlib="$DEPS_DIR/zlib" --with-openssl --disable-ftp --disable-ldap \
                --without-libpsl --without-libidn2 --without-brotli
    make -j$(nproc) && make install
fi

echo "=== TSA: Dependencies Built Successfully ==="
