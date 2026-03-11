# Stage 1: Build
FROM tsa-build-env:u24 AS builder

WORKDIR /build

# We rely on system-provided libraries from the base image
COPY . .

RUN rm -rf build && mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && \
    strip tsp tsa_cli tsa_server_pro tsa_top

# Stage 2: Runtime
FROM ubuntu:24.04

RUN sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libssl3 libpcap0.8t64 libncurses6 libcurl4 libsrt1.5-openssl liblua5.4-0 zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/build/tsp /app/
COPY --from=builder /build/build/tsa_cli /app/tsa
COPY --from=builder /build/build/tsa_server_pro /app/tsa_server
COPY --from=builder /build/build/tsa_top /app/tsa_top
COPY tsa.conf /app/tsa.conf

EXPOSE 8088 12345 9000/udp

ENTRYPOINT ["/app/tsa_server", "/app/tsa.conf"]
