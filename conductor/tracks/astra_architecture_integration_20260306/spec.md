# Track: Astra Architecture Integration (Dynamic Lua Pipeline)

## Objective
Transition TsAnalyzer from a statically compiled, fixed-pipeline tool into a **dynamic, script-programmable stream processing framework** by deeply integrating a Lua runtime, inspired by the architecture of the `Astra` project.
The goal is to allow users to construct complex Transport Stream routing, analysis, and multiplexing graphs (Pipelines) dynamically via Lua scripts without altering the underlying C core.

## Key Requirements

1. **Lua Stream Object Model Binding**
   - Expose the core `tsa_stream_t` (and `tsa_plugin_t`) as first-class Lua objects with metatables.
   - Implement garbage collection (`__gc`) hooks to safely destroy underlying C instances when Lua variables go out of scope.
   - Provide constructors in Lua (e.g., `tsa.udp_input()`, `tsa.analyzer()`, `tsa.udp_output()`).

2. **Graph Topology Construction (Upstream/Downstream Linking)**
   - Expose the C-level stream parent/child API (e.g., `tsa_stream_add_child()`) to Lua.
   - Allow users to write declarations like `my_analyzer:set_upstream(my_input)`.

3. **Reactive PID Filtering via Lua**
   - Allow Lua scripts to subscribe to specific PIDs dynamically (e.g., `my_analyzer:join_pid(0x100)`).
   - This directly interacts with the core engine to bypass parsing overhead for unwanted PIDs.

4. **Event Polling & Callbacks in Lua**
   - Bind the core event dispatcher (`tsa_event.h`) to Lua so scripts can register callback functions for alarms (e.g., `on_sync_loss`, `on_pcr_jitter`).
   - *Stretch Goal*: Lua-driven custom alarms (Lua calculating logic based on raw TS metadata).

## Success Criteria
- [ ] A Lua script can instantiate a UDP input and link it to an analyzer without using the hardcoded C `main()` wrapper.
- [ ] A Lua script can define multiple outputs (e.g., routing PID 0x100 to one IP, and PID 0x200 to another).
- [ ] The engine executes the Lua script natively via a new CLI mode (`tsanalyzer run script.lua`).
- [ ] No memory leaks during Lua garbage collection of stream objects.
