# TsAnalyzer Meta-Makefile
# Wraps CMake commands for easier development workflow

BUILD_DIR = build
CMAKE = cmake
CTEST = ctest
BIN_DIR = $(BUILD_DIR)

.PHONY: all clean test full-test help

all: $(BUILD_DIR)/Makefile
	@$(MAKE) -C $(BUILD_DIR) -j$(shell nproc)

$(BUILD_DIR)/Makefile:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) -DCMAKE_BUILD_TYPE=Release ..

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f test.ts run1.json run2.json final_metrology.json

test: all
	@echo "Running unit tests..."
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure

# Full validation: Unit Tests + Determinism + E2E Smoke
full-test: all
	@echo "=== Starting Full Validation Suite ==="
	@echo "[1/3] Running Unit Tests..."
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure
	@echo "[2/3] Generating Test Stream..."
	@python3 -c "import os; f=open('test.ts', 'wb'); pkt=b'\x47\x00\x00\x10' + b'\xff'*184; [f.write(pkt) for _ in range(20000)]; f.close()"
	@echo "[3/3] Running Determinism and Smoke Tests..."
	@chmod +x scripts/verify_reproducibility.sh scripts/verify_30s_smoke.sh
	@./scripts/verify_reproducibility.sh test.ts
	@./scripts/verify_30s_smoke.sh test.ts
	@echo "=== ALL Roadmaps PASSED ==="

help:
	@echo "TsAnalyzer Build System"
	@echo "Usage:"
	@echo "  make           - Build the project (Release mode)"
	@echo "  make clean     - Remove all build artifacts"
	@echo "  make test      - Run unit tests only"
	@echo "  make full-test - Run complete validation (Unit + Determinism + E2E)"
