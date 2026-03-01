# TsAnalyzer Professional Meta-Makefile
# Standardizes build, test, and quality control workflows

# --- Configuration ---
BUILD_DIR      ?= build
INSTALL_PREFIX ?= /usr/local
CMAKE          ?= cmake
CTEST          ?= ctest
JOBS           ?= $(shell nproc 2>/dev/null || echo 4)

# --- Colors for Output ---
BLUE   := \033[34m
GREEN  := \033[32m
RED    := \033[31m
RESET  := \033[0m

.PHONY: all debug release clean test full-test rt-test install lint format help

all: release

# --- Build Targets ---
debug:
	@echo "$(BLUE)=== Building Debug Version ===$(RESET)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) -DCMAKE_BUILD_TYPE=Debug ..
	@$(MAKE) -C $(BUILD_DIR) -j$(JOBS)

release:
	@echo "$(BLUE)=== Building Release Version ===$(RESET)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) -DCMAKE_BUILD_TYPE=Release ..
	@$(MAKE) -C $(BUILD_DIR) -j$(JOBS)

clean:
	@echo "$(RED)=== Cleaning Build Artifacts ===$(RESET)"
	@rm -rf $(BUILD_DIR)

# --- Test Targets ---
test: release
	@echo "$(GREEN)=== Running Unit Tests (Timeout: 30s) ===$(RESET)"
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure --timeout 30

full-test: release
	@echo "$(GREEN)=== Running Full Validation Suite ===$(RESET)"
	@echo "1. Unit Tests (Timeout: 30s)..."
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure --timeout 30
	@echo "2. Determinism Verification..."
	@chmod +x scripts/verify_determinism.sh
	@./scripts/verify_determinism.sh
	@echo "3. E2E Smoke Test..."
	@chmod +x scripts/verify_30s_smoke.sh
	@./scripts/verify_30s_smoke.sh

rt-test: release
	@echo "$(GREEN)=== Running Real-time Metrology Test (30s) ===$(RESET)"
	@chmod +x scripts/verify_realtime_metrology.sh
	@./scripts/verify_realtime_metrology.sh

# --- Quality & Ops ---
lint:
	@echo "$(BLUE)=== Running Static Analysis ===$(RESET)"
	@cppcheck --enable=all --suppress=missingIncludeSystem --error-exitcode=1 \
		-I include -I deps/mongoose -I deps/srt/srtcore \
		src/*.c include/*.h

format:
	@echo "$(BLUE)=== Formatting Code ===$(RESET)"
	@find src include tests -name "*.c" -o -name "*.h" | xargs clang-format -i

install: release
	@echo "$(BLUE)=== Installing to $(INSTALL_PREFIX) ===$(RESET)"
	@cd $(BUILD_DIR) && $(MAKE) install

help:
	@echo "$(GREEN)TsAnalyzer Build System$(RESET)"
	@echo "Usage:"
	@echo "  make           - Build release version (default)"
	@echo "  make debug     - Build debug version with symbols"
	@echo "  make clean     - Remove all build artifacts"
	@echo "  make test      - Run all unit tests"
	@echo "  make full-test - Run Unit + Determinism + E2E tests"
	@echo "  make rt-test   - Run Real-time Metrology (30s)"
	@echo "  make lint      - Run cppcheck static analysis"
	@echo "  make format    - Apply clang-format"
	@echo "  make install   - Install binaries to $(INSTALL_PREFIX)"
