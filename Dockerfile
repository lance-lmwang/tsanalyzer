# Stage 1: Build
FROM ubuntu:22.04 AS builder

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    tcl-dev \
    libpcap-dev \
    flex \
    bison \
    curl \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source code
COPY . .

# Build dependencies (SRT, libpcap, Lua)
RUN ./build_deps.sh

# Build TsAnalyzer
RUN make -j$(nproc)

# Stage 2: Runtime
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libssl3 \
    libpcap0.8 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binaries from builder
COPY --from=builder /build/build/tsa_cli /app/tsa
COPY --from=builder /build/build/tsa_server_pro /app/tsa_server
COPY --from=builder /build/build/tsa_top /app/tsa_top

# Copy default config
COPY tsa.conf /app/tsa.conf

# Expose default API and Metrics ports
EXPOSE 8088 12345 9000/udp

# Default command
ENTRYPOINT ["/app/tsa_server", "/app/tsa.conf"]
