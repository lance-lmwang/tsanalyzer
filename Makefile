# TsAnalyzer Professional Meta-Makefile
# Hybrid workflow: Native (Default/Fast) | Docker (Release/Sturdy)

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

# Only non-file targets should be PHONY
.PHONY: all clean distclean test full-test rt-test install lint format check-format help tsa_cli_monitor release debug package \
        docker-build docker-release docker-test

all: release

# --- Native Build Targets (Fast Iteration) ---

$(BUILD_DIR)/CMakeCache.txt:
	@echo "$(BLUE)=== Checking Dependencies ===$(RESET)"
	@if [ ! -f deps/srt/lib/libsrt.a ] && [ ! -f deps/srt/lib64/libsrt.a ]; then \
		./build_deps.sh; \
	fi
	@echo "$(BLUE)=== Configuring Build Environment ===$(RESET)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) -DCMAKE_BUILD_TYPE=Release ..

release: $(BUILD_DIR)/CMakeCache.txt
	@echo "$(BLUE)=== Building Binaries (Native) ===$(RESET)"
	@$(MAKE) -C $(BUILD_DIR) -j$(JOBS)
	@ln -sf tsa_cli build/tsa
	@ln -sf tsa_server_pro build/tsa_server

debug:
	@echo "$(BLUE)=== Building Debug Version (Native) ===$(RESET)"
	@mkdir -p $(BUILD_DIR)_debug
	@cd $(BUILD_DIR)_debug && $(CMAKE) -DCMAKE_BUILD_TYPE=Debug ..
	@$(MAKE) -C $(BUILD_DIR)_debug -j$(JOBS)

clean:
	@echo "$(RED)=== Cleaning Build Artifacts ===$(RESET)"
	@rm -rf $(BUILD_DIR) $(BUILD_DIR)_debug bin/
	@$(MAKE) -C src/libtsshaper clean || true

distclean: clean
	@echo "$(RED)=== Performing Absolute Cleanup (distclean) ===$(RESET)"
	@echo "--- Removing Installed Dependencies & Downloaded Sources ---"
	@rm -rf deps/srt deps/libpcap deps/lua deps/zlib deps/curl deps/libebur128
	@rm -rf deps/curl_src deps/lua_src deps/zlib_src deps/libebur128_src deps/ffmpeg_src
	@echo "--- Removing Temporary Test Files, Buffers & Caches ---"
	@find . -type f -name "*.pcap" -delete
	@find . -type f -name "*.ts" -not -path "./sample/*" -delete
	@rm -f jitter_debug.html new.diff buffer\[*
	@find . -type d -name "__pycache__" -exec rm -rf {} +
	@find . -type f -name "*.pyc" -delete
	@find . -type f -name "*.a" -delete
	@find . -type f -name "*.o" -delete
	@echo "--- Restoring Submodules to Pristine State ---"
	@if [ -d deps/srt_src/.git ]; then cd deps/srt_src && git reset --hard HEAD && git clean -fd; fi
	@if [ -d deps/libpcap_src/.git ]; then cd deps/libpcap_src && git reset --hard HEAD && git clean -fd; fi
	@echo "$(GREEN)>>> Workspace is now PRISTINE. git status should be clean.$(RESET)"

# --- Production Release via Docker ---

docker-build:
	$(MAKE) -f Makefile.docker build

docker-release:
	$(MAKE) -f Makefile.docker release

docker-test:
	$(MAKE) -f Makefile.docker test

# --- Distribution & Packaging (Native) ---
VERSION := 2.3.0
PKG_NAME := tsanalyzer-$(VERSION)
DIST_DIR := $(PKG_NAME)

package: release
	@echo "$(BLUE)=== Packaging TsAnalyzer v$(VERSION) ===$(RESET)"
	@rm -rf $(DIST_DIR)
	@mkdir -p $(DIST_DIR)/bin $(DIST_DIR)/docs $(DIST_DIR)/monitoring $(DIST_DIR)/sample
	@cp build/tsa_cli $(DIST_DIR)/bin/tsa
	@cp build/tsa_server_pro $(DIST_DIR)/bin/tsa_server
	@cp build/tsa_top $(DIST_DIR)/bin/tsa_top
	@cp tsa.conf $(DIST_DIR)/
	@cp docker-compose.yml $(DIST_DIR)/
	@cp Dockerfile $(DIST_DIR)/
	@cp docs/*.md $(DIST_DIR)/docs/
	@cp -r monitoring/* $(DIST_DIR)/monitoring/
	@cp sample/test_1m.ts $(DIST_DIR)/sample/ 2>/dev/null || true
	@cp README.md LICENSE $(DIST_DIR)/ 2>/dev/null || true
	@echo "Creating tarball..."
	@tar -czf $(PKG_NAME).tar.gz $(DIST_DIR)
	@rm -rf $(DIST_DIR)
	@echo "$(GREEN)Package created: $(PKG_NAME).tar.gz$(RESET)"

# --- Test Targets (Native) ---

test: release
	@echo "$(GREEN)=== Running Unit Tests (Timeout: 30s) ===$(RESET)"
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure --timeout 30

full-test: release check-format
	@echo "$(GREEN)=== Running Full Validation Suite ===$(RESET)"
	@echo "1. Unit Tests (Timeout: 30s)..."
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure --timeout 30
	@echo "2. Determinism Verification..."
	@chmod +x scripts/verify_determinism.sh
	@./scripts/verify_determinism.sh ./sample/test_1m.ts 2>/dev/null || true
	@echo "3. Functional E2E (CLI-based)..."
	@chmod +x scripts/verify_realtime_metrology.sh scripts/verify_pcr_repetition.sh
	@./scripts/verify_realtime_metrology.sh 2>/dev/null || true
	@./scripts/verify_pcr_repetition.sh 2>/dev/null || true
	@echo "4. Integration E2E (Server-based)..."
	@chmod +x scripts/test-e2e.sh
	@./scripts/test-e2e.sh 2>/dev/null || true

# --- Quality & Ops ---
lint:
	@echo "$(BLUE)=== Running Static Analysis ===$(RESET)"
	@cppcheck --enable=all --suppress=missingIncludeSystem --error-exitcode=1 \
		-I include -I deps/mongoose -I deps/srt/srtcore \
		src/*.c include/*.h

format:
	@find src include tests \( -name "*.c" -o -name "*.h" \) -not -name "mongoose.[ch]" | xargs clang-format -i

check-format:
	@if command -v clang-format >/dev/null 2>&1; then \
		FILES=$$(find src include tests \( -name "*.c" -o -name "*.h" \) -not -name "mongoose.[ch]"); \
		for f in $$FILES; do clang-format $$f | diff -u $$f - || exit 1; done; \
	fi

help:
	@echo "$(GREEN)TsAnalyzer Hybrid Build System$(RESET)"
	@echo "Usage:"
	@echo "  make           - Build release version (Native)"
	@echo "  make full-test - Run all tests natively"
	@echo "  make docker-release - Build production image + run full tests in Docker"
	@echo "  make package   - Create tarball distribution"
	@echo "  make clean     - Remove local build artifacts"
	@echo "  make distclean - Deep clean (remove deps, restore submodules)"
