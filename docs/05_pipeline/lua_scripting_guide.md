# Developer Guide: Dynamic Lua Pipeline (Mode B)

This guide provides a comprehensive onboarding for developers building high-performance, programmable stream processing gateways using TsAnalyzer's embedded Lua engine.

---

## 1. Quick Start: The 5-Minute Gateway

TsAnalyzer Mode B allows you to define your processing topology in a single Lua script.

### Example: Simple Monitoring & Logging
Create a file named `monitor.lua`:

```lua
-- 1. Create primitives
local input = tsa.udp_input(5000)      -- Listen on UDP 5000
local analyzer = tsa.analyzer()        -- Core metrology engine

-- 2. Link topology (Reactive Upstream Model)
analyzer:set_upstream(input)

-- 3. Define reactive logic
analyzer:on('SYNC', function(evt)
    tsa.log("CRITICAL: Sync lost on PID " .. evt.pid .. " | MSG: " .. evt.message)
end)

tsa.log("Gateway started. Waiting for packets...")
```

Run it via CLI:
```bash
./tsanalyzer run monitor.lua
```

---

## 2. Object Model Reference

TsAnalyzer exposes C-level high-performance primitives as Lua **Userdata** objects.

### 2.1 `tsa.source` (Ingestion)
Created via `tsa.udp_input(port)` or `tsa.srt_input(url)`.
*   **Role**: Entry point for TS packets.
*   **Lifecycle**: Automatically managed. When the Lua variable goes out of scope, the C source is stopped and destroyed.

### 2.2 `tsa.analyzer` (Metrology)
Created via `tsa.analyzer()`.
*   **Methods**:
    *   `set_upstream(source)`: Links this analyzer to an input source or another processing node.
    *   `join_pid(pid)`: Opt-in to specific PID analysis (enables hardware-level early filtering).
    *   `drop_pid(pid)`: Explicitly ignore a PID.
    *   `on(event_name, callback)`: Register a function to handle C-core alarms.

### 2.3 `tsa.output` (Egress)
Created via `tsa.udp_output(ip, port)`.
*   **Methods**:
    *   `set_upstream(analyzer)`: Forwards the output of an analyzer (including any mutations).
    *   `set_upstream(source)`: Transparent bypass mode (Direct wire Input -> Output).

---

## 3. The Event Callback System

TsAnalyzer provides a low-latency bridge between the deterministic C metrology engine and the Lua control plane.

### Supported Events
| Event Name | Description | Payload (`evt`) |
| :--- | :--- | :--- |
| `SYNC` | Loss of TS Sync (47h) | `evt.pid`, `evt.message` |
| `CC` | Continuity Counter Error | `evt.pid`, `evt.message` |
| `SCTE35` | Ad-insertion Splice Point | `evt.pid`, `evt.command_type` |
| `TSTD` | Decoder Buffer predictive alarm | `evt.pid`, `evt.ms_to_starve` |

### Callback Signature
```lua
analyzer:on('SYNC', function(evt)
    -- evt.event   : string ("SYNC")
    -- evt.pid     : integer
    -- evt.message : string (details)
end)
```

---

## 4. Advanced Topology: Automatic Failover

```lua
local main_src = tsa.udp_input(5001)
local backup_src = tsa.udp_input(5002)
local analyzer = tsa.analyzer()
local output = tsa.udp_output('10.0.0.1', 6000)

-- Start with Main
analyzer:set_upstream(main_src)
output:set_upstream(analyzer)

analyzer:on('SYNC', function(evt)
    tsa.log("Switching to BACKUP due to Sync Loss...")
    analyzer:set_upstream(backup_src)
    output:set_upstream(analyzer)
end)
```

---

## 5. Performance & Safety Notes

1.  **Zero-Allocation Ingress**: Calling `set_upstream` creates a C-level function pointer link. Data packets flowing through the linked nodes **do not enter the Lua VM**, maintaining 10Gbps line rate.
2.  **Sandbox Restrictions**: The environment is sandboxed. `os.execute` and `io` libraries are disabled for security.
3.  **Garbage Collection**: C objects are bound to Lua proxies. If you nil a Lua variable, the underlying stream node will be cleaned up in the next GC cycle.
