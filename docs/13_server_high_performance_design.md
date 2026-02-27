# TsAnalyzer Pro
## High-Performance Multi-Channel Stream Analysis Server Design

### 1. Hardware Topology & OS Sealing (The Bare-Metal Foundation)

To achieve microsecond determinism at 1 Gbps (830k pps), the Linux kernel must be stripped of all dynamic scheduling, memory management, and power-saving behaviors, acting strictly as a hardware initialization loader for the Data Plane.

#### 1.1 CPU & PCIe Spatial Isolation
* **NUMA-PCIe Binding:** Worker threads and NIC IRQs MUST physically reside on the exact same NUMA Node as the NIC's PCIe slot to eliminate QPI/UPI cross-talk latency.
* **Kernel Fencing (GRUB):** `isolcpus=domain,managed_irq,4-127 rcu_nocbs=4-127`
  (Isolates cores, migrates softirqs, and offloads RCU callbacks).
* **Power & Clock Determinism:** `processor.max_cstate=1 intel_idle.max_cstate=1 pcie_aspm=off`
  (Kills CPU deep sleep wake-up latency and prevents PCIe link state power management delays).

#### 1.2 Memory Defense Mechanisms
* **NUMA Balancing:** `echo 0 > /proc/sys/kernel/numa_balancing` (Prevents background page migration).
* **Paging & Swap:** `sysctl -w vm.swappiness=0 vm.dirty_ratio=5 vm.dirty_background_ratio=2`
* **Huge Pages:** Disable THP (`transparent_hugepage=never`) to avoid page-split stalls, and pre-allocate Static HugeTLB for ring buffers.
* **TLB Shootdown Risk:** `sysctl -w vm.stat_interval=120` (Reduces kernel VM scanning frequency).

#### 1.3 TSC Synchronization Mandate
* Executes a cross-core `tsc_sync_test` at boot. Verifies `invariant_tsc`. If inter-socket offset $> 100ns$, forces Single-NUMA Mode or falls back to `CLOCK_MONOTONIC_RAW` to prevent phantom PCR jitter.

---

### 2. Network Ingest (Zero-Jitter Pipeline)

#### 2.1 The NAPI Busy-Polling Engine

* **Mechanism:** Transitions from an Interrupt-Driven to a Poll-Driven (Deterministic CPU Residency) model.
* **Tuning:** `sysctl -w net.core.busy_poll=50 net.core.busy_read=50`.
* **Micro-Coalescing:** `ethtool -C ethX rx-usecs 1` prevents CPU spin waste during extreme low-traffic valleys.

#### 2.2 RSS & Flow Steering Strategy
* **Strict Affinitization:** Disable RPS/XPS (`echo 0 > /sys/class/net/eth0/queues/rx-0/rps_cpus`).
* **Rule:** NIC RSS Queues count MUST exactly equal Worker Threads count. Each queue is hard-pinned to its respective isolated core.

#### 2.3 Hardware Timestamping
* **Implementation:** `SOF_TIMESTAMPING_RX_HARDWARE`. Extracts the PHY-level nanosecond timestamp via `cmsg`, totally bypassing the OS scheduler for diagnostic-grade PCR math.

---

### 3. Data Plane: The L2-Bound Sparse Router

To completely eliminate L3 eviction and cache line bouncing, the PID router employs a 64B-aligned, Zero-Sentinel Sparse Array that fits entirely within the L2 Cache (~80KB total footprint).

```cpp
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// Level 1: 16KB Index Array (Initialized to 0 to prevent cold-path flush)
alignas(64) uint16_t pid_index[8192] = {0}; 
// Level 2: 64KB Dense Pool (Max 1024 active PIDs)
alignas(64) PIDContext active_pid[1024]; 
uint16_t active_count = 1; // Index 0 is reserved as the "unallocated" sentinel

// CRITICAL: Prevent silent L2 cache blowouts
static_assert(sizeof(PIDContext) == 64, "PIDContext MUST be exactly 64B");

inline void process_ts_packet(const uint8_t* pkt) {
    uint16_t pid = extract_pid(pkt);
    uint16_t idx = pid_index[pid];
    
    // Branchless / Highly predictable fast-path
    if (UNLIKELY(idx == 0)) {
        idx = active_count++;
        pid_index[pid] = idx;
    }
    
    PIDContext* ctx = &active_pid[idx];
    // ... O(1) processing ...
}
```

---

### 4. Diagnostic-Grade PCR Math & FFT Analysis

Standard floats lose precision after 0.6 seconds at 27MHz. This appliance utilizes HFT mathematical models.

#### 4.1 Precision Preservation Algorithms
* **Welford's Online Variance:** Calculates dynamic jitter baselines without catastrophic cancellation. Computes variance incrementally: $\sigma = \sqrt{\frac{M_2}{n-1}}$.
* **Kahan Compensated Summation:** Prevents accumulation drift in Long-Term Linear Regression calculations by maintaining a running compensation for lost low-order bits.

#### 4.2 Tri-Level Time Scales
1. **Short-Term (100ms):** Max excursion tracking.
2. **Medium-Term (1s):** P99 Distribution Histograms.
3. **Long-Term Drift & Periodic Burst:**
   * **Linear Regression:** Predicts encoder clock drift using Kahan-summed 128-bit integers.
   * **FFT Analysis:** Applies a Hann Window ($Window Size = 512$, Amplitude Correction Factor = 2) in the background 1Hz Aggregator to detect low-frequency multiplexer pacing bugs.

---

### 5. Dynamic FSM & Control Plane (Zero Contention)

#### 5.1 Adaptive Noise Floor Hysteresis FSM
Fixed thresholds fail on diverse topologies. The appliance tracks the noise floor ($\mu$) dynamically.
* **Threshold Definition:** $Threshold_{degraded} = \mu + k \cdot \sigma$ (with a hard floor $\sigma_{min} = 50ns$ to prevent false positives on ultra-clean links).
* **UNSTABLE State:** Triggered strictly upon $\ge$ 3 state transitions within a rolling 5-second window.

#### 5.2 Anti-ABA Double-Buffer Aggregator
Eliminates SeqLock cache bouncing entirely. The API/Control Plane NEVER touches the active memory lines of the Data Plane.

1. **Worker (Writer - 100% Lock Free):** Updates inactive buffer.
   `std::atomic_thread_fence(std::memory_order_release);`
   `active_buffer.store(inactive_buf, std::memory_order_release);`
2. **Aggregator (Reader - 1Hz Tick):** `Buffer* current = active_buffer.load(std::memory_order_acquire);`
   * Safely reads the immutable snapshot. Zero atomic retry loops, zero跨 NUMA invalidation.
