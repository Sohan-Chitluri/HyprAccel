# HyprAccel
Graph-based embedded control and acceleration workflow for ESP32-class hardware.

## Features
- **Visual Graph Editor**: Node-based model design for control workflows.
- **Hardware Configuration**: Seamless pin and peripheral setup (SPI, I2C, UART, PWM, ADC, GPIO) and integration via schematic import (KiCad, EasyEDA).
- **Code Generation**: Automated C/C++ generation from graph models.
- **ESP32/PlatformIO Workflow**: Built-in orchestration to compile and flash generated code directly to hardware targets.
- **SDK/Runtime Boundary**: Modular C runtime that isolates target-specific operations from high-level logic.
- **CORDIC Accelerator**: Accelerated trigonometric operations (sine/cosine) with SystemVerilog RTL and C++ reference backends.

## Architecture

HyprAccel consists of several integrated layers mapping visual workflows to physical execution:

```
Hardware Setup
  ↓
Project / Graph
  ↓
Generate
  ↓
Generated C/C++
  ↓
PlatformIO Build
  ↓
Flash
  ↓
ESP32 Runtime
```

### Directory Structure
- `boards/`: Target hardware capability schemas and header generators.
- `mbd/`: Model-Based Design tooling.
  - `editor/`: The Node.js web-based visual editor and backend server.
  - `codegen/`: Translation layer from graph JSON to C source code.
  - `schema/`: JSON Schema definitions for graphs and configurations.
  - `schematic/`: Parsers for KiCad and EasyEDA netlists.
- `sdk/`: Core runtime, node implementations, router, and CORDIC reference.
- `rtl/`: SystemVerilog implementation for the CORDIC hardware accelerator.
- `plat/`: Telemetry dashboards and monitoring tools.
- `reference/`: Golden model definitions (Python) for bit-exact verification.

## Quick Start

### Prerequisites
- Node.js (v18+) and npm
- PlatformIO Core (CLI)
- Physical ESP32 hardware for flashing (e.g., ESP32-WROOM-32)

### Development Server Startup
Start the local HyprAccel MBD Studio server:
```bash
cd mbd/editor
npm install
npm start
```
Navigate to `http://localhost:3000` to access the editor.

### Graph Workflow
1. Create a project and select a hardware target.
2. Define peripheral assignments in the Hardware Setup view, or import a schematic.
3. Build the control logic in the visual Graph Editor. Multiple graphs are supported within a project workspace.
4. Click **Generate Code** to materialize the C/C++ source code.
5. Compile and flash directly to a connected ESP32 using the Project Workspace panel.

### Testing
Run the comprehensive suite of tests in the `mbd/editor`:
```bash
npm --prefix mbd/editor run test:projects
```

## Project Status
HyprAccel is in active development.
- **ESP32 Validation**: Initial ESP32 flashing and physical deployment verification is complete.
- **Hardware-dependent behavior**: Some peripheral endpoints may have stubbed implementations depending on the target board.
- **CORDIC RTL**: Hardware-accelerated Verilator routing and bit-exact FPGA implementation is currently a work-in-progress.
- **Not for Production**: This project is currently an exploratory architecture and is not meant for production deployment.

## Development Documentation
For detailed architectural blueprints, implementation plans, and development history, refer to:
- [PROGRESS.md](PROGRESS.md) - Active roadmap and task tracking
- [Implementation Plan](implementation_plan.md) - Architectural goals
- [Hardware Setup Model](mbd/docs/hardware_setup_model.md)
- [Node Specifications](mbd/nodes/node_spec.md)

## License
MIT License. See [LICENSE](LICENSE) for details.
