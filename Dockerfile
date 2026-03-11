# Stage 1: Build Environment
FROM tsa-build-env:u24 AS builder

WORKDIR /build

# 1. Copy the entire source first
COPY . .

# 2. Build dependencies fresh inside the container
# This ensures deps/srt, deps/libpcap etc. are populated locally within the container
RUN ./build_deps.sh

# 3. Build the main project
# We use -no-pie to ensure compatibility with our internal static libs
RUN rm -rf build && mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && \
    make test && \
    strip tsp tsa_cli tsa_server_pro tsa_top

# Stage 2: Minimal Runtime Image
FROM ubuntu:24.04

RUN sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources

ENV DEBIAN_FRONTEND=noninteractive

# Install bare minimum runtime dependencies
RUN apt-get update && apt-get install -y \
    libssl3 \
    libpcap0.8t64 \
    libncurses6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binaries and config from builder
COPY --from=builder /build/build/tsp /app/
COPY --from=builder /build/build/tsa_cli /app/tsa
COPY --from=builder /build/build/tsa_server_pro /app/tsa_server
COPY --from=builder /build/build/tsa_top /app/tsa_top
COPY tsa.conf /app/tsa.conf

EXPOSE 8088 12345 9000/udp

ENTRYPOINT ["/app/tsa_server", "/app/tsa.conf"]
