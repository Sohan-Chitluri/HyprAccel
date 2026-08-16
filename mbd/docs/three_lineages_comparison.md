# HyprAccel: Three Lineages Comparison

HyprAccel is a Model-Based Design (MBD) tool for industrial IoT, specifically optimized for Indian silicon (THEJAS/Aries CPU + FPGA). It draws inspiration from three distinct industry-standard toolchain lineages, merging their core capabilities into a single open-source environment.

## 1. STM32CubeMX (Peripheral & Pin Configuration)

*   **What it does:** A graphical tool for configuring STM32 microcontrollers. It allows users to set up clock trees, pin multiplexing, and peripheral parameters (SPI, I2C, ADC, etc.), generating C initialization code and project structures.
*   **Hardware/Vendor Lock-in:** Proprietary to **STMicroelectronics**. It is strictly locked to the STM32 ecosystem.
*   **HyprAccel Integration:** HyprAccel adopts the visual pin-mapping philosophy. It provides a "Pin Config" screen that allows developers to visually assign functions to the GPIOs of Indian boards (like the THEJAS-based Shakti or Aries). This eliminates the need for manual register-level configuration while maintaining an open-source path for indigenous hardware.

## 2. Simulink + Embedded Coder (Graph-to-C Logic)

*   **What it does:** A block-diagram environment for multi-domain simulation and Model-Based Design. **Embedded Coder** generates readable, compact, and fast C/C++ code from these models for use on embedded processors.
*   **Hardware/Vendor Lock-in:** Proprietary to **MathWorks**. While the generated C code is portable, the editor, simulation engine, and specialized toolboxes require expensive commercial licenses.
*   **HyprAccel Integration:** HyprAccel provides a lightweight Node Editor that implements the "Graph → C" workflow. Users can drag-and-drop functional nodes (logic, math, filters) to define application behavior. HyprAccel then generates standard C code targeting the THEJAS CPU, providing the benefits of visual logic design without the high cost and complexity of the MathWorks ecosystem.

## 3. Xilinx HDL Coder / System Generator / LabVIEW FPGA (Graph-to-RTL Acceleration)

*   **What it does:** These tools translate high-level graphical models (often from Simulink or LabVIEW) directly into synthesizable RTL (Verilog/VHDL) for FPGA implementation. This allows engineers to deploy complex algorithms onto hardware fabric without deep HDL expertise.
*   **Hardware/Vendor Lock-in:** Heavily locked to specific FPGA vendors. **Xilinx System Generator** is tied to AMD/Xilinx silicon and Vivado; **LabVIEW FPGA** is tied to NI hardware and Xilinx backends.
*   **HyprAccel Integration:** HyprAccel enables hardware/software co-design by allowing certain nodes in the graph to be mapped directly to FPGA accelerators (e.g., the CORDIC core). Instead of a full "Graph-to-RTL" compiler, HyprAccel uses a "Hardware Backend Routing" approach. The node editor manages the data movement between the THEJAS CPU and the FPGA fabric, treating the FPGA as a transparent accelerator for high-performance blocks, thus merging the CPU-side logic and FPGA-side performance into one unified interface.

## Summary: The HyprAccel Advantage

HyprAccel does not seek to achieve 1:1 feature parity with these multi-decade industry standards. Instead, it provides a **focused, open-source alternative** that solves the fragmentation between pin configuration, CPU logic, and FPGA acceleration. 

By merging these three ideas, HyprAccel allows developers targeting Indian silicon to:
1.  **Configure** pins visually (inspired by CubeMX).
2.  **Model** logic graphically (inspired by Simulink).
3.  **Accelerate** via FPGA fabric (inspired by HDL Coder).

All within a single, transparent toolchain designed for the future of Indian industrial electronics.
