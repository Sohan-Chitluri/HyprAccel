PYTHON ?= python3
VERILATOR ?= verilator
BUILD_DIR := build
RTL := ../pkg/cordic_pkg.sv ../fp_add_sub.sv ../cordic_lut.sv ../cordic_stage.sv ../cordic_pipeline.sv ../cordic_top.sv

.PHONY: test vectors clean

vectors:
	mkdir -p $(BUILD_DIR)
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) ../../reference/cordic_golden/cordic_golden.py gen-mem 10000 $(BUILD_DIR)/regression.mem

test: vectors
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-TIMESCALEMOD --top-module cordic_regression_tb --Mdir $(BUILD_DIR)/obj $(RTL) cordic_regression_tb.sv
	$(BUILD_DIR)/obj/Vcordic_regression_tb

clean:
	rm -rf $(BUILD_DIR)
