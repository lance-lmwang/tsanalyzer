# TsAnalyzer: Deployment & Installation Guide

This guide provides step-by-step instructions for building, installing, and deploying TsAnalyzer in both development and production environments.

---

## 1. System Requirements

### Operating System
*   **Linux** (Kernel 5.x or later recommended).
*   Ubuntu 20.04+, Debian 11+, or RHEL/CentOS 8+.

### Hardware Prerequisites
*   **Minimum**: 2 Cores, 4GB RAM.
*   **Production (1Gbps Analysis)**: 8+ Physical Cores (dedicated), 16GB RAM, NIC supporting `SO_TIMESTAMPING` (e.g., Mellanox ConnectX).

---

## 2. Dependencies

Before building, install the mandatory system-level dependencies:

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git libssl-dev tcl-dev libpcap-dev flex bison
```

### RHEL/CentOS
```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake openssl-devel libpcap-devel flex bison
```

---

## 3. Building from Source

TsAnalyzer uses a multi-stage build process to ensure all performance-critical dependencies are linked statically.

### Step 1: Build Third-Party Dependencies
This script downloads and builds SRT, libpcap, and Lua as static libraries.
```bash
chmod +x build_deps.sh
./build_deps.sh
```

### Step 2: Build TsAnalyzer Core
Use the provided `Makefile` for a standard Release build:
```bash
make -j$(nproc)
```

---

## 4. Docker Deployment

For environments where containerization is preferred, TsAnalyzer provides a multi-stage Dockerfile.

### 4.1 Build the Image
```bash
make docker-image
```

### 4.2 Run with Docker Compose (Recommended)
This launches the analyzer along with the full Prometheus/Grafana stack.
```bash
cd monitoring
./monitoring-up.sh
```

---

## 5. Release Packaging

To generate a complete, portable distribution package for customers:
```bash
make package
```
This will create a `tsanalyzer-2.3.0.tar.gz` file containing:
*   **bin/**: Pre-compiled static binaries (`tsa`, `tsa_server`, `tsa_top`).
*   **docs/**: Full technical documentation.
*   **scripts/**: Operational and tuning scripts.
*   **monitoring/**: Dockerized Grafana/Prometheus setup.
*   **tsa.conf**: Default configuration template.

---

## 6. Production Hardening (Appliance Mode)

To achieve instrument-grade precision, the system environment must be tuned.

### 4.1 Real-Time Permissions
Grant the analyzer binary permission to use raw sockets and real-time scheduling without requiring `root`:
```bash
sudo ./scripts/grant_rt_perms.sh
```

### 4.2 System Performance Tuning
Execute the tuning script to disable power-saving states and configure large network buffers:
```bash
sudo ./scripts/sys_tune_perf.sh
```

---

## 5. Deploying the Monitoring Stack

TsAnalyzer integrates natively with Prometheus and Grafana for high-density visualization.

### 5.1 Launch Dockerized Grafana/Prometheus
```bash
cd monitoring
./monitoring-up.sh
```
*   **Grafana**: `http://localhost:3000` (Default credentials: `admin/admin`)
*   **Prometheus**: `http://localhost:9090`

### 5.2 Deploy Dashboards
Automatically instantiate the Three-Plane NOC dashboards:
```bash
python3 scripts/deploy_dashboard.py
```

---

## 6. Quick Start

### Single Stream Analysis (CLI)
```bash
./build/tsa_cli --mode live --srt-url srt://:9000
```

### Multi-Stream Server
1. Create a `tsa.conf`:
   ```text
   CHANNEL-01  srt://:9001?mode=listener
   CHANNEL-02  udp://239.1.1.1:1234
   ```
2. Start the server:
   ```bash
   ./build/tsa_server tsa.conf
   ```

---

## 7. Verification

Run the full verification suite to ensure the installation is correct and deterministic:
```bash
make test      # Runs 100+ unit tests
make full-test # Validates end-to-end integration
```
