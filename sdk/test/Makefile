# HyprAccel CORE-T4 — CORDIC reference test harness build script
#
# Usage (via make if available, or run gcc directly):
#   make test                Run accuracy test
#   make bench               Run benchmark (10M iterations)
#   make bench N=5000000     Custom iteration count
#   make dump                Dump CSV for RTL testbench stimulus
#   make clean               Remove build artifacts
#
# Direct gcc (if make is unavailable):
#   gcc -O2 -std=c99 -I../src -o build/test_cordic test_cordic_ref.c ../src/hyp_cordic_ref.c -lm
#   ./build/test_cordic --test
#   ./build/test_cordic --bench 10000000
#   ./build/test_cordic --dump > stimulus.csv

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c99
LDFLAGS = -lm

SRC_DIR  = ../src
BUILD    = build
TARGET   = $(BUILD)/test_cordic

N ?= 10000000

SRCS = test_cordic_ref.c $(SRC_DIR)/hyp_cordic_ref.c
HDRS = $(SRC_DIR)/hyp_cordic_ref.h

.PHONY: all test bench dump clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(SRCS) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $(SRCS) $(LDFLAGS)

test: $(TARGET)
	@echo ""
	@echo ">>> Running CORDIC accuracy test..."
	@echo ""
	$(TARGET) --test --verbose
	@echo ""
	@echo ">>> Exit code: $$?"

bench: $(TARGET)
	@echo ""
	@echo ">>> Running CORDIC benchmark (N=$(N))..."
	@echo ""
	$(TARGET) --bench $(N)

dump: $(TARGET)
	@echo ">>> Dumping CSV stimulus for RTL testbench..."
	$(TARGET) --dump

clean:
	rm -rf $(BUILD)
