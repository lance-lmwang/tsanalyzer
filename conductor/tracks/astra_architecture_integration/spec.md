# Specification: Astra Architecture Integration

## 1. Background
The existing `tsanalyzer` uses a fixed thread-per-stage pipeline (Capture -> Decode -> Metrology -> Output). This is excellent for predictability but lacks flexibility and scales poorly when dealing with highly dense, multi-program transport streams (MPTS) where many PIDs are irrelevant or when different independent analyses need the exact same stream data simultaneously. The `astra` project demonstrates a highly efficient `Stream Tree` and reactive `PID management` approach that solves these exact issues.

## 2. Goals
- **Goal 1:** Implement a Stream Tree Architecture where TS packets are passed via function pointers (callbacks) to dynamically registered observer modules (the "Tree" or "Pipeline").
- **Goal 2:** Implement a Plugin Module System where different analyzer engines (TR 101 290, Codec, PCR, etc.) are encapsulated as plugins.
- **Goal 3:** Implement Reactive PID Management to drop unwanted TS packets at the ingress point.

## 3. Design Principles
- **Zero-Copy Routing:** Stream distribution must pass pointers to the 188-byte buffer; no deep copies unless required by a specific module.
- **Backwards Compatibility:** Initial implementation should not break existing functionality or CLI inputs; it should just change internal routing.
