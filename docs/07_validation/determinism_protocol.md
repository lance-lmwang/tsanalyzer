# TsAnalyzer: Determinism & Regression Protocol

## 1. The Metrology Contract
TsAnalyzer Pro guarantees **Bit-Identical Analysis Results** across identical inputs. This is achieved by anchoring all calculations to the **Hardware Arrival Time (HAT)** instead of the system clock.

## 2. Quantitative Determinism
- **Formula**: `Input(Packets + HAT) + Engine(Commit_ID) = Output(JSON_Snapshot)`
- **Deviation Limit**: 0.0ns (Zero jitter deviation across re-runs of the same capture).

## 3. Regression Testing
Every build is validated against a 1GB "Golden Master" TS capture.
- The generated JSON must match the `master_baseline.json` with an MD5 hash match.
- This ensures that optimizations (like SIMD) do not alter the analytical integrity of the TR 101 290 engine.
