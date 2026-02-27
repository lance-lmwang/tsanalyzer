#!/bin/bash
# TsAnalyzer Pro: Enterprise Kernel Hardening Script
# Based on Tencent Cloud High-Perf Architecture Design

if [ "$EUID" -ne 0 ]; then 
    echo "CRITICAL: This script must be run as root to modify kernel parameters."
    exit 1
fi

echo "[*] Phase 1: NUMA & TLB Suppression..."
echo 0 > /proc/sys/kernel/numa_balancing
sysctl -w vm.stat_interval=120
if [ -f /sys/kernel/mm/transparent_hugepage/enabled ]; then
    echo never > /sys/kernel/mm/transparent_hugepage/enabled
fi

echo "[*] Phase 2: Static HugeTLB Allocation..."
sysctl -w vm.nr_hugepages=1024

echo "[*] Phase 3: NAPI Busy Poll Activation (SoftIRQ Pull Model)..."
sysctl -w net.core.busy_poll=50
sysctl -w net.core.busy_read=50

echo "[*] Phase 4: NIC RSS/XPS/RPS Discipline..."
# Attempt to find the primary interface (excluding lo)
INTERFACE=$(ip route | grep default | awk '{print $5}' | head -n 1)
if [ -z "$INTERFACE" ]; then INTERFACE="eth0"; fi

echo "[*] Optimizing interface: $INTERFACE"
ethtool -K "$INTERFACE" tx-nocache-copy on 2>/dev/null || echo "Warning: ethtool tx-nocache-copy not supported."

# Disable RPS/XPS for deterministic worker processing
for rx_queue in /sys/class/net/"$INTERFACE"/queues/rx-*; do
    echo 0 > "$rx_queue"/rps_cpus 2>/dev/null || true
done

echo "[*] Phase 5: Network Stack Buffer Hardening..."
sysctl -w net.core.rmem_max=33554432
sysctl -w net.core.wmem_max=33554432
sysctl -w net.core.rmem_default=16777216
sysctl -w net.core.wmem_default=16777216
sysctl -w net.core.netdev_max_backlog=30000

echo "--------------------------------------------------"
echo "✅ KERNEL HARDENING COMPLETE"
