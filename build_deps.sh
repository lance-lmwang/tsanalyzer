#!/bin/bash
set -e

# Get the absolute path of the directory where the script is located
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"

PCAP_VERSION="1.10.4"
PCAP_DIR="$DEPS_DIR/libpcap"
PCAP_INSTALL_DIR="$PCAP_DIR"

SRT_VERSION="v1.5.3"
SRT_DIR="$DEPS_DIR/srt"
SRT_INSTALL_DIR="$SRT_DIR"

echo "=== TSA: Building Third-party Dependencies (Static) ==="
mkdir -p "$DEPS_DIR"

# 1. Build SRT (Priority, as it is core)
if [ ! -d "$SRT_DIR" ]; then
    echo "--- Downloading SRT $SRT_VERSION ---"
    cd "$DEPS_DIR"
    git clone --depth 1 --branch "$SRT_VERSION" https://github.com/Haivision/srt.git srt
fi

echo "--- Building SRT (Static) ---"
cd "$SRT_DIR"
mkdir -p build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX="$SRT_INSTALL_DIR" \
         -DCMAKE_INSTALL_LIBDIR=lib \
         -DENABLE_SHARED=OFF \
         -DENABLE_STATIC=ON \
         -DENABLE_APPS=OFF \
         -DENABLE_TESTING=OFF \
         -DUSE_STATIC_LIBSTDCXX=ON
make -j$(nproc)
make install

# 2. Build libpcap (Skip if it fails due to missing flex/lex)
if [ ! -d "$PCAP_DIR" ]; then
    echo "--- Downloading libpcap $PCAP_VERSION ---"
    cd "$DEPS_DIR"
    curl -L "https://www.tcpdump.org/release/libpcap-$PCAP_VERSION.tar.gz" -o libpcap.tar.gz
    tar -xvf libpcap.tar.gz
    mv "libpcap-$PCAP_VERSION" libpcap
    rm libpcap.tar.gz
fi

echo "--- Building libpcap (Static) ---"
cd "$PCAP_DIR"
mkdir -p build
cd build
if cmake .. -DENABLE_SHARED=OFF -DCMAKE_INSTALL_PREFIX="$PCAP_INSTALL_DIR" -DBUILD_WITH_LIBNL=OFF; then
    make -j$(nproc)
    make install
    echo "PCAP Built Successfully."
else
    echo "WARNING: libpcap build failed (probably missing flex/bison). PCAP support will be limited."
fi

# 3. Build Lua (Static)
LUA_VERSION="5.4.6"
LUA_DIR="$DEPS_DIR/lua"
if [ ! -d "$LUA_DIR" ]; then
    echo "--- Downloading Lua $LUA_VERSION ---"
    cd "$DEPS_DIR"
    curl -L -R -O "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
    tar -zxf "lua-$LUA_VERSION.tar.gz"
    mv "lua-$LUA_VERSION" lua
    rm "lua-$LUA_VERSION.tar.gz"
fi
if [ ! -f "$LUA_DIR/src/liblua.a" ]; then
    echo "--- Building Lua (Static) ---"
    cd "$LUA_DIR"
    make linux -j$(nproc)
fi

# 4. Build Zlib (Static - Required for curl/HLS)
ZLIB_VERSION="1.3.1"
ZLIB_DIR="$DEPS_DIR/zlib"
if [ ! -d "$ZLIB_DIR" ]; then
    echo "--- Downloading Zlib $ZLIB_VERSION ---"
    cd "$DEPS_DIR"
    curl -L "https://github.com/madler/zlib/releases/download/v$ZLIB_VERSION/zlib-$ZLIB_VERSION.tar.gz" -o zlib.tar.gz
    tar -xvf zlib.tar.gz
    mv "zlib-$ZLIB_VERSION" zlib
    rm zlib.tar.gz
fi
echo "--- Building Zlib (Static) ---"
cd "$ZLIB_DIR"
./configure --prefix="$ZLIB_DIR" --static
make -j$(nproc)
make install

# 5. Build Libcurl (Static)
CURL_VERSION="8.6.0"
CURL_DIR="$DEPS_DIR/curl"
CURL_INSTALL_DIR="$CURL_DIR"
if [ ! -d "$CURL_DIR" ]; then
    echo "--- Downloading Curl $CURL_VERSION ---"
    cd "$DEPS_DIR"
    curl -L "https://curl.se/download/curl-$CURL_VERSION.tar.gz" -o curl.tar.gz
    tar -xvf curl.tar.gz
    mv "curl-$CURL_VERSION" curl
    rm curl.tar.gz
fi
echo "--- Building Curl (Static) ---"
cd "$CURL_DIR"
./configure --prefix="$CURL_INSTALL_DIR" \
            --enable-static \
            --disable-shared \
            --disable-ftp \
            --disable-ldap \
            --disable-ldaps \
            --disable-rtsp \
            --disable-proxy \
            --disable-dict \
            --disable-telnet \
            --disable-tftp \
            --disable-pop3 \
            --disable-imap \
            --disable-smb \
            --disable-smtp \
            --disable-gopher \
            --disable-mqtt \
            --with-zlib="$ZLIB_DIR" \
            --with-openssl \
            --without-libpsl \
            --without-libidn2
make -j$(nproc)
make install

echo "=== TSA: Dependencies Built ==="
[ -f "$SRT_INSTALL_DIR/lib/libsrt.a" ] || [ -f "$SRT_INSTALL_DIR/lib64/libsrt.a" ] || { echo "SRT build failed!"; exit 1; }

echo "SRT Static lib: Found"
[ -f "$PCAP_INSTALL_DIR/lib/libpcap.a" ] && echo "PCAP Static lib: Found" || echo "PCAP Static lib: NOT FOUND (Optional)"
[ -f "$LUA_DIR/src/liblua.a" ] && echo "Lua Static lib: Found" || { echo "Lua build failed!"; exit 1; }
[ -f "$ZLIB_DIR/lib/libz.a" ] && echo "Zlib Static lib: Found" || { echo "Zlib build failed!"; exit 1; }
[ -f "$CURL_DIR/lib/libcurl.a" ] && echo "Curl Static lib: Found" || { echo "Curl build failed!"; exit 1; }

exit 0
