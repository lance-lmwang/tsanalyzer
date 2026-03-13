# Best Practice Blueprints: Production Topologies

This document provides verified configuration templates and Lua topologies for common industrial use cases.

---

## Blueprint A: Cloud OTT Ingest Audit (HLS/SRT)
**Scenario**: You are receiving a contribution feed via SRT and re-packaging it into HLS. You need to ensure the source is compliant before it hits the packager.

*   **Topology**: `SRT Listener -> Analyzer (TR 101 290 + T-STD) -> Prometheus Exporter`
*   **Lua Logic**:
```lua
local input = tsa.srt_input("srt://:9000?mode=listener")
local analyzer = tsa.analyzer()
analyzer:set_upstream(input)

-- Enable SCTE-35 alignment check
analyzer:on('SCTE35', function(evt)
    tsa.log("Ad-Marker detected on PID " .. evt.pid)
end)
```

---

## Blueprint B: Remote Distribution Guard (Pacing & Failover)
**Scenario**: Sending a stream over a jittery long-haul network. You need to smooth the bitrate and provide a backup source.

*   **Topology**: `Dual UDP Inputs -> Smart Failover -> Bitrate Smoother -> SRT Caller`
*   **Key Config (`tsa.conf`)**:
```nginx
stream "main_channel" {
    input "udp://239.1.1.1:5000";
    backup "udp://239.1.1.2:5000";

    pipeline {
        shaping cbr;
        target_bitrate 15Mbps;
        output "srt://dist-node.local:9000?mode=caller";
    }
}
```

---

## Blueprint C: 10Gbps Backbone Passive Probe
**Scenario**: Monitoring hundreds of multicast groups on a high-capacity link without impacting traffic.

*   **Topology**: `NIC Mirror Port -> DPDK/AF_PACKET Ingest -> Multi-stream Metrology`
*   **Performance Note**: Use `numactl` to pin the process to the NIC's NUMA node.
```bash
numactl --cpunodebind=0 --membind=0 ./tsa_server_pro --config cluster.conf
```

---

## Blueprint D: Ad-Insertion Forensic Audit
**Scenario**: Automatically capturing evidence when ad-insertion (SCTE-35) fails to align with video I-frames.

*   **Lua Logic**:
```lua
analyzer:on('SCTE35_MISALIGN', function(evt)
    tsa.log("Misalignment detected! Triggering 20s Micro-capture...")
    tsa.trigger_capture(evt.stream_id, 20) -- 10s pre + 10s post
end)
```
