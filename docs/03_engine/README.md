# Engine Documentation: T-STD Multiplexer

This directory contains the authoritative specifications and operational guides for the T-STD multiplexer implementation.

## Core Specifications
- [**Architecture Specification**](./tstd_architecture_specification.md): Authoritative design of the modular physical timing model and VBV management.
- [**Hierarchical Scheduler**](./tstd_scheduler.md): Detailed logic of the mutually-exclusive decision tree and preemption tiers.

## Operational & Debugging Guides
- [**Pitfalls and Tuning Guide**](./tstd_pitfalls_and_tuning.md): An extensive knowledge base covering 12 major engineering "craters" and their definitive solutions.

## Summary of Key Features
*   **Opportunistic SI Injection**: Zero-jitter system information delivery by utilizing idle slots.
*   **Delay-Adaptive PI Control**: Intelligent bandwidth regulation that scales with buffer depth.
*   **State-Decoupled Feedback**: Immune to "state poisoning" for long-term 24/7 stability.
*   **DVM Voter System**: Multi-PID consensus mechanism for robust timestamp discontinuity handling.
