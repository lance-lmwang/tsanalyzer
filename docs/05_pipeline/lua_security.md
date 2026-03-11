# Security Hardening: Lua Sandbox (Mode B)

To ensure the integrity of the TsAnalyzer appliance, the embedded Lua environment is strictly sandboxed to prevent malicious or accidental system interference.

---

## 1. Function Whitelist & Blacklist

### ❌ Disabled (Dangerous Functions)
- `os.execute`, `os.rename`, `os.remove`, `os.getenv`
- `io.*` (All file system access is blocked)
- `debug.*` (Hooks and introspection are disabled)
- `dofile`, `loadfile` (Only pre-vetted local files can be run via CLI)

### ✅ Enabled (Stream Logic Only)
- Standard math, string, and table libraries.
- The `tsa.*` factory objects (Source, Analyzer, Output).

## 2. Resource Exhaustion Protection
- **Memory Quota**: Each Lua state is capped at 64MB of heap.
- **Instruction Limit**: (Planned) CPU time slicing to prevent infinite Lua loops from stalling the Data Plane.
