# Performance Tuning Guide: Reaching 10Gbps+

This guide provides technical instructions for optimizing the Linux host environment to support TsAnalyzer's maximum analytical throughput.

---

## 1. NUMA Locality (The Golden Rule)

TsAnalyzer is NUMA-aware. Performance drops by ~30% if packets cross the UPI/QPI interconnect between physical CPU sockets.

### Identification
Check your NIC's affinity:
```bash
cat /sys/class/net/eth0/device/numa_node
```

### Optimization
Ensure the `worker_threads` are pinned to cores on the same NUMA node as the NIC. Use `taskset` or `numactl`:
```bash
numactl --cpunodebind=0 --membind=0 ./tsanalyzer run topology.lua
```

---

## 2. NIC Ring & IRQ Tuning

### 2.1 Ring Buffer Size
Increase RX ring buffers to prevent drops during micro-bursts:
```bash
ethtool -G eth0 rx 4096
```

### 2.2 IRQ Affinity
Disable `irqbalance` and manually pin NIC interrupts to specific cores (separate from Metrology Workers):
```bash
systemctl stop irqbalance
# Example: Pin eth0 interrupts to core 0
echo 1 > /proc/irq/$(grep eth0 /proc/interrupts | awk '{print $1}' | tr -d ':')/smp_affinity
```

---

## 3. Kernel Parameter Hardening (`sysctl`)

Add the following to `/etc/sysctl.conf` for high-PPS UDP ingestion:

```ini
# Increase socket read buffer
net.core.rmem_max = 33554432
net.core.rmem_default = 33554432

# Increase max pending packets
net.core.netdev_max_backlog = 10000

# Enable timestamps for metrology
net.core.tstamp_allow_data = 1
```

---

## 4. Hugepages Configuration

For large-scale stream tables, Hugepages reduce the overhead of page table lookups.
```bash
# Allocate 1024 hugepages of 2MB each
echo 1024 > /proc/sys/vm/nr_hugepages
```
Configure TsAnalyzer to use them via `tsa.conf`:
```nginx
system {
    use_hugepages on;
}
```
