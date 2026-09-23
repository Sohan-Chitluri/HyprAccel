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

.PHONY: all test bench dump clean objs rtl-backend-test board-config-test sensor-actuator-test pid-test spi-i2c-test pinfunc-test

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

# SDK-T6: SPI/I2C transaction primitives, exercised on the host against the
# Arduino/SPI/Wire stubs in mock/. Compiled as C++ (the real hyp_esp32_hw.cpp is
# C++) with the mock include dir taking precedence over any system Arduino headers.
SPI_I2C_TEST_CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -Imock -I$(INC_DIR) -I$(SRC_DIR)
spi-i2c-test: test_spi_i2c_transact.cpp $(SRC_DIR)/hyp_esp32_hw.cpp $(INC_DIR)/hyprccel.h $(INC_DIR)/hyp_esp32_hw.h ../../boards/codegen/hyp_board_config.h | $(BUILD)
	$(CXX) $(SPI_I2C_TEST_CXXFLAGS) -o $(BUILD)/test_spi_i2c_transact test_spi_i2c_transact.cpp $(SRC_DIR)/hyp_esp32_hw.cpp
	$(BUILD)/test_spi_i2c_transact

# Desktop pin-function/clock init gating (hyp_esp32_hw.cpp HYP_SDK_PINFUNC_MODE).
# hyp_esp32_hw.cpp includes its board config header via the hardcoded relative
# path "../../boards/codegen/hyp_board_config.h", resolved relative to
# hyp_esp32_hw.cpp's OWN location, not the compiler's invocation directory —
# so to compile it against a fixture header instead of the real committed one,
# each variant mirrors the repo's sdk/src + boards/codegen layout into a
# scratch tree under $(BUILD)/pinfunc_mirror/<variant>/ and copies the real
# hyp_esp32_hw.cpp plus the desired fixture header into it.
PINFUNC_TEST_CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -Imock -I$(INC_DIR) -I$(SRC_DIR)
PINFUNC_MIRROR = $(BUILD)/pinfunc_mirror

$(PINFUNC_MIRROR)/desktop/sdk/src/hyp_esp32_hw.cpp: $(SRC_DIR)/hyp_esp32_hw.cpp
	mkdir -p $(dir $@) && cp $< $@
$(PINFUNC_MIRROR)/desktop/boards/codegen/hyp_board_config.h: fixtures/board_config_pinfunc.h
	mkdir -p $(dir $@) && cp $< $@

$(PINFUNC_MIRROR)/nonstd/sdk/src/hyp_esp32_hw.cpp: $(SRC_DIR)/hyp_esp32_hw.cpp
	mkdir -p $(dir $@) && cp $< $@
$(PINFUNC_MIRROR)/nonstd/boards/codegen/hyp_board_config.h: fixtures/board_config_pinfunc_nonstd.h
	mkdir -p $(dir $@) && cp $< $@

$(PINFUNC_MIRROR)/minimal/sdk/src/hyp_esp32_hw.cpp: $(SRC_DIR)/hyp_esp32_hw.cpp
	mkdir -p $(dir $@) && cp $< $@
$(PINFUNC_MIRROR)/minimal/boards/codegen/hyp_board_config.h: fixtures/board_config_pinfunc_minimal.h
	mkdir -p $(dir $@) && cp $< $@

$(PINFUNC_MIRROR)/gpio_console/sdk/src/hyp_esp32_hw.cpp: $(SRC_DIR)/hyp_esp32_hw.cpp
	mkdir -p $(dir $@) && cp $< $@
$(PINFUNC_MIRROR)/gpio_console/boards/codegen/hyp_board_config.h: fixtures/board_config_pinfunc_gpio_console.h
	mkdir -p $(dir $@) && cp $< $@
$(PINFUNC_MIRROR)/mixed_uart0/sdk/src/hyp_esp32_hw.cpp: $(SRC_DIR)/hyp_esp32_hw.cpp
	mkdir -p $(dir $@) && cp $< $@
$(PINFUNC_MIRROR)/mixed_uart0/boards/codegen/hyp_board_config.h: fixtures/board_config_pinfunc_gpio1_uart0_rx.h
	mkdir -p $(dir $@) && cp $< $@

pinfunc-test: test_pinfunc_gating.cpp $(INC_DIR)/hyprccel.h $(INC_DIR)/hyp_esp32_hw.h \
              $(PINFUNC_MIRROR)/desktop/sdk/src/hyp_esp32_hw.cpp $(PINFUNC_MIRROR)/desktop/boards/codegen/hyp_board_config.h \
              $(PINFUNC_MIRROR)/nonstd/sdk/src/hyp_esp32_hw.cpp $(PINFUNC_MIRROR)/nonstd/boards/codegen/hyp_board_config.h \
              $(PINFUNC_MIRROR)/minimal/sdk/src/hyp_esp32_hw.cpp $(PINFUNC_MIRROR)/minimal/boards/codegen/hyp_board_config.h \
              $(PINFUNC_MIRROR)/gpio_console/sdk/src/hyp_esp32_hw.cpp $(PINFUNC_MIRROR)/gpio_console/boards/codegen/hyp_board_config.h \
              $(PINFUNC_MIRROR)/mixed_uart0/sdk/src/hyp_esp32_hw.cpp $(PINFUNC_MIRROR)/mixed_uart0/boards/codegen/hyp_board_config.h \
              ../../boards/codegen/hyp_board_config.h | $(BUILD)
	@echo ""
	@echo ">>> [legacy] no PINFUNC macros -> unchanged full init"
	$(CXX) $(PINFUNC_TEST_CXXFLAGS) -DHYP_PINFUNC_VARIANT_LEGACY -o $(BUILD)/test_pinfunc_legacy test_pinfunc_gating.cpp $(SRC_DIR)/hyp_esp32_hw.cpp
	$(BUILD)/test_pinfunc_legacy
	@echo ""
	@echo ">>> [desktop] PINFUNC + standard 160 MHz clock -> selective init"
	$(CXX) $(PINFUNC_TEST_CXXFLAGS) -DHYP_PINFUNC_VARIANT_DESKTOP -o $(BUILD)/test_pinfunc_desktop test_pinfunc_gating.cpp $(PINFUNC_MIRROR)/desktop/sdk/src/hyp_esp32_hw.cpp
	$(BUILD)/test_pinfunc_desktop
	@echo ""
	@echo ">>> [nonstd] PINFUNC + nonstandard 100 MHz clock -> warns, doesn't fail"
	$(CXX) $(PINFUNC_TEST_CXXFLAGS) -DHYP_PINFUNC_VARIANT_NONSTD -o $(BUILD)/test_pinfunc_nonstd test_pinfunc_gating.cpp $(PINFUNC_MIRROR)/nonstd/sdk/src/hyp_esp32_hw.cpp
	$(BUILD)/test_pinfunc_nonstd
	@echo ""
	@echo ">>> [minimal] PINFUNC, single assigned pin, no clock -> nothing else inits"
	$(CXX) $(PINFUNC_TEST_CXXFLAGS) -DHYP_PINFUNC_VARIANT_MINIMAL -o $(BUILD)/test_pinfunc_minimal test_pinfunc_gating.cpp $(PINFUNC_MIRROR)/minimal/sdk/src/hyp_esp32_hw.cpp
	$(BUILD)/test_pinfunc_minimal
	@echo ""
	@echo ">>> [gpio_console] PINFUNC, GPIO1+GPIO3 assigned plain GPIO (UART0 unassigned) -> UART0 never enabled, console silenced"
	$(CXX) $(PINFUNC_TEST_CXXFLAGS) -DHYP_PINFUNC_VARIANT_GPIO_CONSOLE -o $(BUILD)/test_pinfunc_gpio_console test_pinfunc_gating.cpp $(PINFUNC_MIRROR)/gpio_console/sdk/src/hyp_esp32_hw.cpp
	$(BUILD)/test_pinfunc_gpio_console
	@echo ""
	@echo ">>> [mixed_uart0] PINFUNC, GPIO1 plain GPIO + GPIO3 UART0 RX -> GPIO1 configured, UART0 not active, console off"
	$(CXX) $(PINFUNC_TEST_CXXFLAGS) -DHYP_PINFUNC_VARIANT_MIXED_UART0 -o $(BUILD)/test_pinfunc_mixed_uart0 test_pinfunc_gating.cpp $(PINFUNC_MIRROR)/mixed_uart0/sdk/src/hyp_esp32_hw.cpp
	$(BUILD)/test_pinfunc_mixed_uart0

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
