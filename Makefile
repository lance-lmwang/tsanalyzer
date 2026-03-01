# TsAnalyzer Meta-Makefile
# Wraps CMake commands for easier development workflow

BUILD_DIR = build
CMAKE = cmake
CTEST = ctest
BIN_DIR = $(BUILD_DIR)

.PHONY: all clean test full-test rt-test help

# ... (rest of Makefile)

rt-test: all
	@echo "=== Running Real-time Metrology Test (30s) ==="
	@chmod +x scripts/verify_realtime_metrology.sh
	@./scripts/verify_realtime_metrology.sh

help:
	@echo "TsAnalyzer Build System"
	@echo "Usage:"
	@echo "  make           - Build the project"
	@echo "  make clean     - Clean build artifacts"
	@echo "  make test      - Run unit tests"
	@echo "  make full-test - Run Unit + Determinism + E2E Smoke"
	@echo "  make rt-test   - Run Real-time PCR-driven Metrology (30s)"
