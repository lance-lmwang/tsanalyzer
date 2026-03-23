# Testing & Validation Guide: libtsshaper

## 1. Overview
`libtsshaper` utilizes a **Virtual Time Domain** validation strategy to ensure 100% deterministic compliance testing without being affected by OS scheduling noise or CI/CD resource fluctuations.

## 2. The Verification Pipeline
The pipeline consists of three stages:
1.  **Test Bench**: A C program that drives the shaper with mock VBR packets.
2.  **Virtual HAL**: Intercepts the pacer and generates a high-precision PCAP file with nanosecond timestamps.
3.  **Python Referee**: Analyzes the PCAP and asserts PCR Jitter/Interval compliance.

## 3. Usage
To run the full end-to-end jitter validation:

```bash
cd src/libtsshaper
mkdir -p build && cd build
cmake ..
make check-jitter
```

### 3.1 Automated Audit
The `make check-jitter` command will:
- Execute `test_bench` to produce `virtual_test.pcap`.
- Invoke `scripts/pcr_analyzer.py` to audit the file.
- Exit with code 0 if **Max Jitter < 30ns**.

## 4. Manual Debugging & Visualization
If a test fails, you can generate an interactive visualization to identify the jitter pattern:

```bash
# From the project root
python3 scripts/pcr_analyzer.py src/libtsshaper/build/virtual_test.pcap --pid 0x100 --plot jitter_report.html
```
Open `jitter_report.html` in any browser to see the nanosecond-level deviation trend.

## 5. Mock Data Generation
For testing the analyzer itself, a standalone mock generator is provided:
```bash
gcc -o pcap_mock tools/pcapng_mock_gen.c
./pcap_mock sample.pcap 50 # Generates 50ns intentional jitter
```
