#!/bin/bash
set -e

# Get the absolute path of the directory where the script is located
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"

echo "=== TSA: Building Third-party Dependencies (Static) ==="
mkdir -p "$DEPS_DIR"

# Helper to clean up submodule after build to prevent "dirty" status
cleanup_submodule() {
    local dir=$1
    if [ -d "$dir/.git" ]; then
        echo "--- Cleaning up submodule: $dir ---"
        (cd "$dir" && git reset --hard HEAD && git clean -fd)
    fi
}

# 1. Build SRT
SRT_VERSION="v1.5.3"
SRT_SRC_DIR="$DEPS_DIR/srt_src"
SRT_INSTALL_DIR="$DEPS_DIR/srt"
SRT_BUILD_DIR="$DEPS_DIR/srt_build_tmp"

if [ ! -d "$SRT_SRC_DIR" ]; then
    echo "--- Downloading SRT $SRT_VERSION ---"
    git clone --depth 1 --branch "$SRT_VERSION" https://github.com/Haivision/srt.git "$SRT_SRC_DIR"
fi

echo "--- Building SRT (Static) ---"
mkdir -p "$SRT_BUILD_DIR"
cd "$SRT_BUILD_DIR"
cmake "$SRT_SRC_DIR" -DCMAKE_INSTALL_PREFIX="$SRT_INSTALL_DIR" \
         -DCMAKE_INSTALL_LIBDIR=lib \
         -DENABLE_SHARED=OFF \
         -DENABLE_STATIC=ON \
         -DENABLE_APPS=OFF \
         -DENABLE_TESTING=OFF \
         -DUSE_STATIC_LIBSTDCXX=ON
make -j$(nproc)
make install
cd "$PROJECT_ROOT"
rm -rf "$SRT_BUILD_DIR"
cleanup_submodule "$SRT_SRC_DIR"

# 2. Build libpcap
PCAP_VERSION="1.10.4"
PCAP_SRC_DIR="$DEPS_DIR/libpcap_src"
PCAP_INSTALL_DIR="$DEPS_DIR/libpcap"
PCAP_BUILD_DIR="$DEPS_DIR/pcap_build_tmp"

if [ ! -d "$PCAP_SRC_DIR" ]; then
    echo "--- Downloading libpcap $PCAP_VERSION ---"
    cd "$DEPS_DIR"
    curl -L "https://www.tcpdump.org/release/libpcap-$PCAP_VERSION.tar.gz" -o libpcap.tar.gz
    tar -xvf libpcap.tar.gz
    DIR_NAME=$(tar -tf libpcap.tar.gz | head -1 | cut -f1 -d"/")
    mv "$DIR_NAME" libpcap_src
    rm libpcap.tar.gz
fi

echo "--- Building libpcap (Static) ---"
mkdir -p "$PCAP_BUILD_DIR"
cd "$PCAP_BUILD_DIR"
cmake "$PCAP_SRC_DIR" -DENABLE_SHARED=OFF -DCMAKE_INSTALL_PREFIX="$PCAP_INSTALL_DIR" -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_WITH_LIBNL=OFF
make -j$(nproc)
make install
cd "$PROJECT_ROOT"
rm -rf "$PCAP_BUILD_DIR"
cleanup_submodule "$PCAP_SRC_DIR"

# 3. Build Lua
LUA_VERSION="5.4.6"
LUA_SRC_DIR="$DEPS_DIR/lua_src"
LUA_INSTALL_DIR="$DEPS_DIR/lua"
if [ ! -d "$LUA_SRC_DIR" ]; then
    echo "--- Downloading Lua $LUA_VERSION ---"
    cd "$DEPS_DIR"
    curl -L -R -O "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
    tar -zxf "lua-$LUA_VERSION.tar.gz"
    DIR_NAME=$(tar -tf "lua-$LUA_VERSION.tar.gz" | head -1 | cut -f1 -d"/")
    mv "$DIR_NAME" lua_src
    rm "lua-$LUA_VERSION.tar.gz"
fi
echo "--- Building Lua (Static) ---"
cd "$LUA_SRC_DIR"
make clean || true
make posix -j$(nproc) || make generic -j$(nproc)
mkdir -p "$LUA_INSTALL_DIR/include" "$LUA_INSTALL_DIR/lib"
cp src/lua.h src/luaconf.h src/lualib.h src/lauxlib.h src/lua.hpp "$LUA_INSTALL_DIR/include/" || true
cp src/liblua.a "$LUA_INSTALL_DIR/lib/"
make clean
cd "$PROJECT_ROOT"

# 4. Build Zlib
ZLIB_SRC_DIR="$DEPS_DIR/zlib_src"
ZLIB_INSTALL_DIR="$DEPS_DIR/zlib"
if [ -d "$ZLIB_SRC_DIR" ]; then
    echo "--- Building Zlib (Static) ---"
    cd "$ZLIB_SRC_DIR"
    ./configure --prefix="$ZLIB_INSTALL_DIR" --static
    make -j$(nproc)
    make install
    make distclean || true
    cd "$PROJECT_ROOT"
fi

# 5. Build Libcurl (Static)
CURL_SRC_DIR="$DEPS_DIR/curl_src"
CURL_INSTALL_DIR="$DEPS_DIR/curl"
ZLIB_DIR="$DEPS_DIR/zlib"
if [ -d "$CURL_SRC_DIR" ]; then
    echo "--- Building Curl (Static) ---"
    cd "$CURL_SRC_DIR"
    [ -f Makefile ] && make distclean || true
    ./configure --prefix="$CURL_INSTALL_DIR" \
                --enable-static --disable-shared --disable-ftp --disable-ldap --disable-ldaps \
                --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp \
                --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher \
                --disable-mqtt --with-zlib="$ZLIB_DIR" --with-openssl \
                --without-libpsl --without-libidn2 --without-brotli --without-zstd --without-librtmp
    make -j$(nproc)
    make install
    make distclean || true
    cd "$PROJECT_ROOT"
fi

# 6. Build libebur128 (Static)
EBUR128_SRC_DIR="$DEPS_DIR/libebur128_src"
EBUR128_INSTALL_DIR="$DEPS_DIR/libebur128"
EBUR128_BUILD_DIR="$DEPS_DIR/libebur128_build_tmp"
if [ -d "$EBUR128_SRC_DIR" ]; then
    echo "--- Building libebur128 (Static) ---"
    mkdir -p "$EBUR128_BUILD_DIR"
    cd "$EBUR128_BUILD_DIR"
    cmake "$EBUR128_SRC_DIR" -DCMAKE_INSTALL_PREFIX="$EBUR128_INSTALL_DIR" \
             -DCMAKE_INSTALL_LIBDIR=lib \
             -DBUILD_STATIC_LIBS=ON \
             -DBUILD_SHARED_LIBS=OFF \
             -DENABLE_INTERNAL_QUEUE=ON
    make -j$(nproc)
    make install
    cd "$PROJECT_ROOT"
    rm -rf "$EBUR128_BUILD_DIR"
fi

echo "=== TSA: All Dependencies Built and Installed Cleanly ==="
