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

.PHONY: all debug release clean test full-test rt-test install lint format help test-e2e test-e2d test-chaos

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

# --- End-to-End Dashboard Test ---
test-e2e: release
	@echo "$(GREEN)=== Running End-to-End Dashboard Test ===$(RESET)"
	@chmod +x scripts/test_big_screen_e2e.sh
	@./scripts/test_big_screen_e2e.sh

test-e2d: test-e2e

# --- Live Streaming Demo ---
demo: release
	@echo "$(GREEN)=== Starting Live Stream Analysis Demo ===$(RESET)"
	@pkill -9 tsa || true
	@pkill -9 tsp || true
	@./build/tsp -i 127.0.0.1 -p 19001 -b 10000000 -l -f sample/test_1m.ts > /dev/null 2>&1 &
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
	@./build/tsp -i 127.0.0.1 -p 19001 -P -l -f sample/cctvhd.ts > /dev/null 2>&1 &
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
	            "name": "CCTV5",
	            "url": "udp://@:19001"
	        },
	        {
	            "name": "SPORT_HD",
	            "url": "udp://@:19002"
	        }
	    ]
	}
	EOF
	@./build/tsp -i 127.0.0.1 -p 19001 -P -l -f sample/btvhd.ts > /dev/null 2>&1 &
	@./build/tsp -i 127.0.0.1 -p 19002 -P -l -f sample/cctvhd.ts > /dev/null 2>&1 &
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
	@./build/tsa --mode=replay sample/mpts_4prog.ts
	@echo "$(GREEN)Analysis Complete.$(RESET)"

help:
	@echo "$(GREEN)TsAnalyzer Build System$(RESET)"
	@echo "Usage:"
	@echo "  make           - Build release version (default)"
	@echo "  make debug     - Build debug version with symbols"
	@echo "  make clean     - Remove all build artifacts"
	@echo "  make test      - Run all unit tests"
	@echo "  make full-test - Run Unit + Determinism + E2E tests"
	@echo "  make demo      - Run 15s Live Analysis Demo (tsp -> tsa -> python monitor)"
	@echo "  make test-e2e  - Run End-to-End Dashboard Test"
	@echo "  make test-chaos - Run Automated Fault Injection Test"
	@echo "  make rt-test   - Run Real-time Metrology (30s)"
	@echo "  make lint      - Run cppcheck static analysis"
	@echo "  make format    - Apply clang-format"
	@echo "  make install   - Install binaries to $(INSTALL_PREFIX)"
