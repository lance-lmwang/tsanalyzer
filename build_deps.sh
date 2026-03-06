#!/bin/bash
set -e

# 获取脚本所在目录的绝对路径
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

# 1. 编译 SRT (优先，因为它是核心)
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

# 2. 编译 libpcap (如果由于缺少 flex/lex 失败，则跳过)
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

echo "=== TSA: Dependencies Built ==="
[ -f "$SRT_INSTALL_DIR/lib/libsrt.a" ] || [ -f "$SRT_INSTALL_DIR/lib64/libsrt.a" ] || { echo "SRT build failed!"; exit 1; }

echo "SRT Static lib: Found"
[ -f "$PCAP_INSTALL_DIR/lib/libpcap.a" ] && echo "PCAP Static lib: Found" || echo "PCAP Static lib: NOT FOUND (Optional)"

exit 0
