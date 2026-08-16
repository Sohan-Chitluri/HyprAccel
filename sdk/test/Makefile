# HyprAccel CORE-T4 — CORDIC reference test harness build script
#
# Usage (via make if available, or run gcc directly):
#   make test                Run accuracy test (math.h check)
#   make validate            Run bit-exact validation against JSON vectors
#   make bench               Run benchmark (10M iterations)
#   make bench N=5000000     Custom iteration count
#   make dump                Dump CSV for RTL testbench stimulus
#   make clean               Remove build artifacts
#
# Direct gcc (if make is unavailable):
#   gcc -O2 -std=c99 -I../include -I../src -o build/test_cordic test_cordic_ref.c ../src/hyp_cordic_ref.c -lm
#   gcc -O2 -std=c99 -I../include -I../src -c ../src/hyp_thejas.c -o build/hyp_thejas.o
#   gcc -O2 -std=c99 -I../include -I../src -c ../src/hyp_esp32.c -o build/hyp_esp32.o
#   gcc -O2 -std=c99 -I../include -I../src -c ../src/hyp_router.c -o build/hyp_router.o
#   ./build/test_cordic --test
#   ./build/test_cordic --bench 10000000
#   ./build/test_cordic --dump > stimulus.csv

CC      = gcc
INC_DIR = ../include
SRC_DIR = ../src
CFLAGS  = -O2 -Wall -Wextra -std=c99 -I$(INC_DIR) -I$(SRC_DIR)
LDFLAGS = -lm

BUILD    = build
TARGET   = $(BUILD)/test_cordic

N ?= 10000000

SRCS = test_cordic_ref.c $(SRC_DIR)/hyp_cordic_ref.c
HDRS = $(SRC_DIR)/hyp_cordic_ref.h $(INC_DIR)/hyprccel.h

SDK_OBJS = $(BUILD)/hyp_cordic_ref.o $(BUILD)/hyp_thejas.o $(BUILD)/hyp_esp32.o $(BUILD)/hyp_router.o

.PHONY: all test bench dump clean objs

all: $(TARGET) objs

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(SRCS) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

objs: $(SDK_OBJS)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

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

validate: $(TARGET)
	@echo ""
	@echo ">>> Running CORDIC bit-exact validation against 10k vectors..."
	@echo ""
	$(TARGET) --validate ../../reference/cordic_golden/vectors/regression_10k.json
	@echo ""
	@echo ">>> Exit code: $$?"

dump: $(TARGET)
	@echo ">>> Dumping CSV stimulus for RTL testbench..."
	$(TARGET) --dump

clean:
	rm -rf $(BUILD)
