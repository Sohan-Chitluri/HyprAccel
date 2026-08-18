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
CXX     = g++
VERILATOR ?= verilator
INC_DIR = ../include
SRC_DIR = ../src
CFLAGS  = -O2 -Wall -Wextra -std=c99 -I$(INC_DIR) -I$(SRC_DIR)
LDFLAGS = -lm

BUILD    = build
TARGET   = $(BUILD)/test_cordic
RTL_OBJ  = $(BUILD)/rtl_obj
RTL_TEST = $(BUILD)/test_cordic_rtl_backend
RTL_ROOT = ../../rtl
RTL_SRCS = $(RTL_ROOT)/pkg/cordic_pkg.sv $(RTL_ROOT)/fp_add_sub.sv $(RTL_ROOT)/cordic_lut.sv $(RTL_ROOT)/cordic_stage.sv $(RTL_ROOT)/cordic_pipeline.sv $(RTL_ROOT)/cordic_top.sv
VERILATOR_ROOT ?= $(shell $(VERILATOR) -V 2>/dev/null | grep VERILATOR_ROOT | head -1 | sed -e 's/.*=[[:space:]]*//')
RTL_CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -DHYP_ENABLE_VERILATOR_BACKEND -I$(INC_DIR) -I$(SRC_DIR) -I$(RTL_OBJ) -I$(VERILATOR_ROOT)/include

N ?= 10000000

SRCS = test_cordic_ref.c $(SRC_DIR)/hyp_cordic_ref.c
HDRS = $(SRC_DIR)/hyp_cordic_ref.h $(INC_DIR)/hyprccel.h

SDK_OBJS = $(BUILD)/hyp_cordic_ref.o $(BUILD)/hyp_thejas.o $(BUILD)/hyp_esp32.o $(BUILD)/hyp_router.o $(BUILD)/hyp_pid.o $(BUILD)/hyp_encoder.o
BOARD_CONFIG_TEST = $(BUILD)/test_board_config_consumption

.PHONY: all test bench dump clean objs rtl-backend-test board-config-test sensor-actuator-test pid-test

all: $(TARGET) objs $(BOARD_CONFIG_TEST)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(SRCS) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

objs: $(SDK_OBJS)

$(BUILD)/hyp_encoder.o: $(SRC_DIR)/hyp_encoder.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hyp_pid.o: $(SRC_DIR)/hyp_pid.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	@echo ""
	@echo ">>> Running CORDIC accuracy test..."
	@echo ""
	$(TARGET) --test --verbose
	@echo ""
	@echo ">>> Exit code: $$?"

$(BOARD_CONFIG_TEST): test_board_config_consumption.c $(INC_DIR)/hyprccel.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_board_config_consumption.c

board-config-test: $(BOARD_CONFIG_TEST)
	@echo ""
	@echo ">>> Running FW-P1..P6 board config consumption tests..."
	@echo ""
	$(BOARD_CONFIG_TEST)
	@echo ""
	@echo ">>> Exit code: $$?"

sensor-actuator-test: test_sensor_actuator.c $(INC_DIR)/hyprccel.h | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test_sensor_actuator test_sensor_actuator.c
	$(BUILD)/test_sensor_actuator

pid-test: test_pid.c $(INC_DIR)/hyprccel.h $(BUILD)/hyp_pid.o | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test_pid test_pid.c $(BUILD)/hyp_pid.o $(LDFLAGS)
	$(BUILD)/test_pid

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

$(RTL_OBJ)/Vcordic_top.mk: $(RTL_SRCS) | $(BUILD)
	$(VERILATOR) --cc --Mdir $(RTL_OBJ) --top-module cordic_top $(RTL_SRCS)

$(RTL_OBJ)/Vcordic_top__ALL.a: $(RTL_OBJ)/Vcordic_top.mk
	$(MAKE) -C $(RTL_OBJ) -f Vcordic_top.mk Vcordic_top__ALL.a

$(BUILD)/hyp_router_rtl.o: $(SRC_DIR)/hyp_router.c | $(BUILD)
	$(CC) $(CFLAGS) -DHYP_ENABLE_VERILATOR_BACKEND -c $< -o $@

$(RTL_TEST): test_cordic_rtl_backend.cpp $(SRC_DIR)/hyp_cordic_rtl_backend.cpp $(SRC_DIR)/hyp_cordic_rtl_backend.h $(BUILD)/hyp_router_rtl.o $(BUILD)/hyp_esp32.o $(BUILD)/hyp_cordic_ref.o $(BUILD)/hyp_thejas.o $(INC_DIR)/hyprccel.h $(RTL_OBJ)/Vcordic_top__ALL.a
	$(CXX) $(RTL_CXXFLAGS) -o $@ test_cordic_rtl_backend.cpp $(SRC_DIR)/hyp_cordic_rtl_backend.cpp $(BUILD)/hyp_router_rtl.o $(BUILD)/hyp_esp32.o $(BUILD)/hyp_cordic_ref.o $(BUILD)/hyp_thejas.o $(RTL_OBJ)/Vcordic_top__ALL.a $(VERILATOR_ROOT)/include/verilated.cpp $(VERILATOR_ROOT)/include/verilated_threads.cpp

rtl-backend-test: $(RTL_TEST)
	$(RTL_TEST)

clean:
	rm -rf $(BUILD)
