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

# Only non-file targets should be PHONY
.PHONY: all clean test full-test rt-test install lint format help tsa_cli_monitor

all: release

# --- File-based Build Targets ---

# Sentinel file to track CMake configuration
$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	@echo "$(BLUE)=== Checking Dependencies ===$(RESET)"
	@if [ ! -f deps/srt/lib/libsrt.a ] && [ ! -f deps/srt/lib64/libsrt.a ]; then \
		./build_deps.sh; \
	fi
	@echo "$(BLUE)=== Configuring Build Environment ===$(RESET)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) -DCMAKE_BUILD_TYPE=Release ..

# The actual binaries depend on the CMake config and source files
$(BUILD_DIR)/tsa_cli $(BUILD_DIR)/tsa_server $(BUILD_DIR)/tsa_server_pro: $(BUILD_DIR)/CMakeCache.txt src/*.c include/*.h tests/*.c
	@echo "$(BLUE)=== Building Binaries ===$(RESET)"
	@$(MAKE) -C $(BUILD_DIR) -j$(JOBS)
	@ln -sf tsa_cli build/tsa

# release is now a simple alias for the binaries
release: $(BUILD_DIR)/tsa_cli $(BUILD_DIR)/tsa_server $(BUILD_DIR)/tsa_server_pro

# --- Distribution & Packaging ---
VERSION := 2.3.0
PKG_NAME := tsanalyzer-$(VERSION)
DIST_DIR := $(PKG_NAME)

package: release
	@echo "$(BLUE)=== Packaging TsAnalyzer v$(VERSION) ===$(RESET)"
	@rm -rf $(DIST_DIR)
	@mkdir -p $(DIST_DIR)/bin $(DIST_DIR)/docs $(DIST_DIR)/scripts $(DIST_DIR)/monitoring $(DIST_DIR)/sample
	@cp build/tsa_cli $(DIST_DIR)/bin/tsa
	@cp build/tsa_server_pro $(DIST_DIR)/bin/tsa_server
	@cp build/tsa_top $(DIST_DIR)/bin/tsa_top
	@cp tsa.conf $(DIST_DIR)/
	@cp docs/*.md $(DIST_DIR)/docs/
	@cp scripts/*.sh scripts/*.py $(DIST_DIR)/scripts/
	@cp -r monitoring/* $(DIST_DIR)/monitoring/
	@cp sample/test_1m.ts $(DIST_DIR)/sample/
	@cp README.md LICENSE $(DIST_DIR)/ 2>/dev/null || true
	@echo "Creating tarball..."
	@tar -czf $(PKG_NAME).tar.gz $(DIST_DIR)
	@rm -rf $(DIST_DIR)
	@echo "$(GREEN)Package created: $(PKG_NAME).tar.gz$(RESET)"

docker-image:
	@echo "$(BLUE)=== Building Docker Image ===$(RESET)"
	docker build -t tsanalyzer:$(VERSION) .
	docker tag tsanalyzer:$(VERSION) tsanalyzer:latest

debug:
	@echo "$(BLUE)=== Building Debug Version ===$(RESET)"
	@mkdir -p $(BUILD_DIR)_debug
	@cd $(BUILD_DIR)_debug && $(CMAKE) -DCMAKE_BUILD_TYPE=Debug ..
	@$(MAKE) -C $(BUILD_DIR)_debug -j$(JOBS)

clean:
	@echo "$(RED)=== Cleaning Build Artifacts ===$(RESET)"
	@rm -rf $(BUILD_DIR) $(BUILD_DIR)_debug

# --- Test Targets ---

test: release
	@echo "$(GREEN)=== Running Unit Tests (Timeout: 30s) ===$(RESET)"
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure --timeout 30

# Comprehensive validation including functional and integration tests
full-test: release
	@echo "$(GREEN)=== Running Full Validation Suite ===$(RESET)"
	@echo "1. Unit Tests (Timeout: 30s)..."
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure --timeout 30
	@echo "2. Determinism Verification..."
	@chmod +x scripts/verify_determinism.sh
	@./scripts/verify_determinism.sh ./sample/test_1m.ts
	@echo "3. Functional E2E (CLI-based)..."
	@chmod +x scripts/verify_realtime_metrology.sh scripts/verify_pcr_repetition.sh
	@./scripts/verify_realtime_metrology.sh
	@./scripts/verify_pcr_repetition.sh
	@echo "4. Integration E2E (Server-based)..."
	@chmod +x scripts/test-e2e.sh
	@./scripts/test-e2e.sh

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

# --- End-to-End Dashboard Test ---
test-e2e: release
	@echo "$(GREEN)=== Running End-to-End Dashboard Test ===$(RESET)"
	@chmod +x scripts/test_big_screen_e2e.sh
	@./scripts/test_big_screen_e2e.sh

# --- Live Streaming Demo ---
demo: release
	@echo "$(GREEN)=== Starting Live Stream Analysis Demo ===$(RESET)"
	@pkill -9 tsa || true
	@pkill -9 tsp || true
	@./build/tsp -i 127.0.0.1 -p 19001 -b 10000000 -l -f ./sample/test_1m.ts > /dev/null 2>&1 &
	@./build/tsa --mode=live --udp 19001 > tsa_live.log 2>&1 &
	@echo "Waiting for engine warmup..."
	@sleep 2
	@chmod +x scripts/tsa_monitor.py
	@./scripts/tsa_monitor.py --duration 15
	@pkill -9 tsa || true
	@pkill -9 tsp || true
	@echo "$(GREEN)Demo Complete.$(RESET)"

# --- Real-time CLI Monitor ---
tsa_cli_monitor: release
	@echo "$(GREEN)=== Starting TsAnalyzer CLI Live Monitor (PCR-Locked) ===$(RESET)"
	@pkill -9 tsa || true
	@pkill -9 tsp || true
	@./build/tsp -i 127.0.0.1 -p 19001 -P -l -f sample/test.ts > /dev/null 2>&1 &
	@./build/tsa --mode=live --udp 19001 > tsa_live.log 2>&1 &
	@echo "Waiting for engine warmup..."
	@sleep 5
	@chmod +x scripts/tsa_monitor.py
	@./scripts/tsa_monitor.py --duration 30
	@pkill -9 tsa || true
	@pkill -9 tsp || true
	@echo "$(GREEN)Monitor Session Complete.$(RESET)"

# --- Real-time Server Monitor (Multi-Stream) ---
tsa_server_monitor: release
	@echo "$(GREEN)=== Starting TsAnalyzer Server Multi-Stream Monitor (PCR-Locked) ===$(RESET)"
	@pkill -9 tsa || true
	@pkill -9 tsp || true
	@cat << EOF > test_server.conf
	{
	    "http_port": 8088,
	    "metrics_path": "/metrics",
	    "expert_mode": false,
	    "nodes": [
	        {
	            "name": "STREAM_1",
	            "url": "udp://@:19001"
	        },
	        {
	            "name": "STREAM_2",
	            "url": "udp://@:19002"
	        }
	    ]
	}
	EOF
	@./build/tsp -i 127.0.0.1 -p 19001 -P -l -f sample/test.ts > /dev/null 2>&1 &
	@./build/tsp -i 127.0.0.1 -p 19002 -P -l -f sample/test.ts > /dev/null 2>&1 &
	@./build/tsa_server test_server.conf > tsa_server.log 2>&1 &
	@echo "Waiting for engine warmup..."
	@sleep 5
	@./scripts/tsa_monitor.py --url http://localhost:8088/api/v1/snapshot --duration 40
	@pkill -9 tsa || true
	@pkill -9 tsp || true
	@rm test_server.conf
	@echo "$(GREEN)Server Monitor Session Complete.$(RESET)"

# --- Offline File Analysis (Replay Mode) ---
tsa_file_report: release
	@echo "$(GREEN)=== Starting TsAnalyzer Offline Forensic Analysis ===$(RESET)"
	@./build/tsa --mode=replay sample/test.ts
	@echo "$(GREEN)Analysis Complete.$(RESET)"

# --- High-Density CLI Monitor (SHM-based) ---
tsa_top: release
	@echo "$(GREEN)=== Running TsAnalyzer Pro Top Monitor ===$(RESET)"
	@pkill -9 tsa_server_pro || true
	@pkill -9 tsa_generator || true
	@echo "GLOBAL http_port 8081" > top_test.conf
	@echo "ST-TOP udp://127.0.0.1:20001" >> top_test.conf
	@build/tsa_server_pro top_test.conf > top_server.log 2>&1 & \
	 SERVER_PID=$$!; \
	 build/tsa_generator -i 127.0.0.1 -p 20001 -b 10000000 > /dev/null 2>&1 & \
	 GEN_PID=$$!; \
	 trap "kill $$SERVER_PID $$GEN_PID 2>/dev/null; rm top_test.conf; echo \"$(GREEN)Cleaned up.$(RESET)\"; exit" INT TERM; \
	 sleep 1; \
	 echo "$(GREEN)Launching tsa_top (Press 'q' to exit)...$(RESET)"; \
	 build/tsa_top; \
	 kill $$SERVER_PID $$GEN_PID 2>/dev/null; \
	 rm top_test.conf; \
	 echo "$(GREEN)Session Complete.$(RESET)"

help:
	@echo "$(GREEN)TsAnalyzer Build System$(RESET)"
	@echo "Usage:"
	@echo "  make           - Build release version (if needed)"
	@echo "  make test      - Run all unit tests"
	@echo "  make full-test - Run Unit + Determinism + E2E tests"
	@echo "  make rt-test   - Run Real-time Metrology (30s)"
	@echo "  make lint      - Run cppcheck static analysis"
	@echo "  make clean     - Remove all build artifacts"
