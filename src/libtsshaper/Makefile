# LibTSShaper Standalone Makefile
# Professional PID-specific T-STD Smoothing Engine

CC      ?= gcc
AR      ?= ar
CFLAGS  ?= -O3 -Wall -Wextra -Wno-unused-parameter -fPIC -g
LDFLAGS ?= -pthread -lm

# --- Configuration ---
INC_DIRS := include src/core src/platform src/pacer
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

SRC_CORE := src/core/tsshaper.c \
            src/core/tstd_model.c \
            src/core/statmux.c \
            src/core/interleaver.c \
            src/core/spsc_queue.c

SRC_PACER := src/pacer/pacer_loop.c

SRC_PLATFORM := src/platform/hal.c \
                src/platform/hal_linux.c \
                src/platform/hal_mock.c \
                src/platform/hal_callback.c

SOURCES := $(SRC_CORE) $(SRC_PACER) $(SRC_PLATFORM)
OBJECTS := $(SOURCES:.c=.o)

LIB_NAME := libtsshaper.a

# --- Test Definitions ---
TEST_SRCS := tests/test_bench.c \
             tests/test_real_file_shaping.c \
             tests/test_ffmpeg_integration.c \
             tests/test_offline_export.c

TEST_BINS := $(TEST_SRCS:.c=)

# --- Colors for Output ---
BLUE   := \033[34m
GREEN  := \033[32m
RED    := \033[31m
RESET  := \033[0m

.PHONY: all clean test lint format help check-jitter

all: $(LIB_NAME) $(TEST_BINS)

$(LIB_NAME): $(OBJECTS)
	@echo "$(BLUE)=== Archiving $(LIB_NAME) ===$(RESET)"
	$(AR) rcs $@ $(OBJECTS)

%.o: %.c
	@echo "$(BLUE)Compiling $<...$(RESET)"
	$(CC) $(CFLAGS) $(INC_FLAGS) -c $< -o $@

# Build each test binary
tests/%: tests/%.c $(LIB_NAME)
	@echo "$(BLUE)Building test $@...$(RESET)"
	$(CC) $(CFLAGS) $(INC_FLAGS) $< -o $@ $(LIB_NAME) $(LDFLAGS)

# --- Operational Targets ---

test: $(TEST_BINS)
	@echo "$(GREEN)=== Running LibTSShaper Unit Tests ===$(RESET)"
	@for test in $(TEST_BINS); do \
		echo "$(BLUE)Running $$test...$(RESET)"; \
		./$$test || exit 1; \
	done
	@echo "$(GREEN)All tests passed!$(RESET)"

check-jitter: tests/test_bench
	@echo "$(GREEN)=== Running Professional PCR Jitter Audit ===$(RESET)"
	./tests/test_bench
	@if [ -f ../../scripts/pcr_analyzer.py ]; then \
		python3 ../../scripts/pcr_analyzer.py virtual_test.pcap --pid 0x100; \
	else \
		echo "$(RED)Warning: pcr_analyzer.py not found at ../../scripts/$(RESET)"; \
	fi

lint:
	@echo "$(BLUE)=== Running Static Analysis (cppcheck) ===$(RESET)"
	cppcheck --enable=all --suppress=missingIncludeSystem --error-exitcode=1 \
		$(INC_FLAGS) src/

format:
	@echo "$(BLUE)=== Formatting Source Code ===$(RESET)"
	find . \( -name "*.c" -o -name "*.h" \) -not -path "./build/*" | xargs clang-format -i

clean:
	@echo "$(RED)=== Cleaning libtsshaper Artifacts ===$(RESET)"
	rm -f $(OBJECTS) $(LIB_NAME) $(TEST_BINS)
	rm -f virtual_test.pcap shaper_real_test.ts shaper_offline_test.ts

help:
	@echo "$(GREEN)LibTSShaper Standalone Build System$(RESET)"
	@echo "Usage:"
	@echo "  make           - Build library and all tests"
	@echo "  make test      - Run unit tests"
	@echo "  make check-jitter - Run PCR jitter compliance audit"
	@echo "  make lint      - Run static analysis"
	@echo "  make format    - Format code using clang-format"
	@echo "  make clean     - Remove build artifacts"
