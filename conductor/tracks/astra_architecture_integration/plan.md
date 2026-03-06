# Implementation Plan: Astra Architecture Integration

## Phase 1: Stream Tree & Plugin Module System (Astra Highlights 1 & 5)
- [x] **Step 1:** Define the Stream Tree Core Structures. Create `include/tsa_stream.h` and `src/tsa_stream.c`. Define a `tsa_stream_t` node with `on_ts` callbacks and child attachment logic.
- [x] **Step 2:** Define the Plugin Module API. Create `include/tsa_plugin.h`. Define a generic plugin structure (`tsa_plugin_t`) that integrates with the Stream Tree.
- [x] **Step 3:** Implement the Stream Tree API. Develop `tsa_stream_init`, `tsa_stream_attach`, and `tsa_stream_send` functions, heavily mirroring Astra's `module_stream_t`.
- [x] **Step 4:** Refactor `tsa_engine` initialization and packet processing to use the Stream Tree as the central routing mechanism, replacing the hardcoded pipeline loop for modules.

## Phase 2: Reactive PID Management (Astra Highlight 2)
- [x] **Step 1:** Add `join_pid` and `leave_pid` callbacks and a PID subscription counter array to the `tsa_stream_t` structure.
- [x] **Step 2:** Implement PID demand propagation in `src/tsa_stream.c` (child joining a PID notifies parent).
- [x] **Step 3:** Implement conditional parsing in the root ingest node to immediately drop TS packets corresponding to un-subscribed PIDs.

## Phase 3: Reactor Pattern & Event Loop (Astra Highlight 3)
- [x] **Step 1:** Prototype a lightweight Reactor event loop using `epoll` or equivalent.
- [ ] **Step 2:** Move socket/input reading logic from blocking threads to the event loop.

## Phase 4: Lua-C Hybrid Scripting (Astra Highlight 4)
- [ ] **Step 1:** Integrate embedded Lua state.
- [ ] **Step 2:** Expose module instantiation and Stream Tree attachment as Lua bindings.
