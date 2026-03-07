# Track: V3 Architecture - Programmable Pipeline

## 1. Goal
Migrate `tsanalyzer` to a carrier-grade programmable pipeline architecture with hierarchical configuration and tap-model isolation.

## 2. Phase 1: Foundation & Configuration
- [x] **Task 1.1: Unit Conversion Engine (`tsa_units`)**
    - Complete two-way conversion for `Mbps`, `ms`, `ns`, and `on/off`.
- [x] **Task 1.2: Lexer & Parser Implementation**
    - Implement `tsa_conf_lexer` for tokenization.
    - Implement `tsa_conf_parser` for block-based parsing and VHost inheritance.
- [x] **Task 1.3: Integration**
    - Replace legacy config parsing in `tsa_server.c` and `tsa_cli.c`.

## 3. Phase 2: Data Plane & Pipeline Framework
- [ ] **Task 2.1: Memory Model (Slab & Zero-Copy)**
    - Implement 188-byte Packet Pool and metadata reference counting.
- [ ] **Task 2.2: Threading Model (Reactor-Worker)**
    - Implement non-blocking IO Reactor and affinity-bound Worker Pool.
- [ ] **Task 2.3: Pipeline Stages**
    - Define Stage interface and implement dynamic assembly (tap model).

## 4. Phase 3: Metrology & Action Engine
- [ ] **Task 3.1: 4-Clock Domain Sync**
    - Implement drift calculation between Wall, Arrival, Reference, and Media domains.
- [ ] **Task 3.2: Action Engine Refinement**
    - Implement modular CC Repair, PCR Restamping, and precision Pacer.

## 5. Phase 4: Observability & Cloud-Native
- [ ] **Task 4.1: Unified Metrics & Logging**
    - Re-namespace all Prometheus metrics.
    - Implement JSON structured logging.
- [ ] **Task 4.2: Health & Persistence**
    - Implement `/health` endpoint and `tsa_state.json` persistence.
