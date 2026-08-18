# HyprAccel Task Progress

*DEMO SCOPE, 2026-08-17: live demo targets ESP32 physical target (THEJAS/FPGA simulated via Verilator). Robotic arm demoed via pre-recorded video (MyCobot 280), not live. Hardware model remains generic with target-specific backends.*

*Note: Owner field = the AI tool/person actually executing this task, update on every status change.*
*General Fixes: SDK include path fixed (-I sdk/include); root .gitignore updated for build artifacts; JS scoping/IIFE protection added to graph editor.*

## TRACK CORE — Core SDK & Descriptors
- [x] CORE-T1 | API header | Owner: Antigravity | Status: Complete
- [x] CORE-T2 | boards.yaml schema | Owner: Antigravity | Status: Complete
- [x] CORE-T3 | yaml→header codegen | Owner: Antigravity | Status: Complete
- [x] CORE-T4 | CORDIC ref C impl | Owner: Antigravity | Status: Complete (Revised to match confirmed RTL parameters: 8-stage Q4.12, bit-exact verified against 10k vectors)
- [x] CORE-T5 | THEJAS sw path | Owner: Antigravity | Status: Complete
- [x] CORE-T6 | ESP32 sw path | Owner: Antigravity | Status: Complete
- [x] CORE-T7 | routing layer | Owner: Antigravity | Status: Complete

## TRACK RTL — Verilator & FPGA Target
- [x] RTL-T1 | validate existing RTL | Owner: Antigravity | Status: Complete — Identified root cause of 10,000-vector regression failure: `cordic_stage.sv` embedded corrupted fixed-point angle constants for STAGE_IDX 1..5. Corrected stage angles to exact `round(atan(2^-i) * 4096)` Q12 constants `[3217, 1899, 1003, 509, 256, 128, 64, 32]`. Verified 10,000/10,000 vectors bit-exact pass against reference/cordic_golden/cordic_golden.py (0 mismatches).
- [x] RTL-T1.5 | Verilator-backed hardware path | Owner: Antigravity | Status: Complete — Integrated `cordic_top` RTL with the SDK `HYP_TARGET_HARDWARE` route via host-side Verilator C++ backend (`sdk/src/hyp_cordic_rtl_backend.cpp`). Replaced dummy stubs. Verified graph codegen targets hardware appropriately. Host-side RTL simulation.
- [ ] RTL-T1b | Cordic-Accelerator-SIH RTL evaluation & alignment | Owner: Antigravity | Status: Not started — External CORDIC RTL repo (`/home/peskybird/Projects/Cordic-Accelerator-SIH`) audited. Identified 8-stage Q4.12 pipeline with SVA assertions, Vivado synthesis/STA scripts, and AXI-Stream/AXI-Lite wrappers. Evaluation for cross-repository baseline alignment deferred to post-demo.
- [ ] RTL-T2 | register interface | Owner: Antigravity | Status: Deferred (Decision: SPI for THEJAS<->FPGA, out of scope for current demo round)
- [ ] RTL-T3 | synth + timing | Owner: Antigravity | Status: Blocked on Q4 (waiting for physical target FPGA board, deferred)
- [ ] RTL-T4 | host-side driver | Owner: Antigravity | Status: Deferred (out of scope for current demo round)
- [ ] RTL-T5 | latency measurement | Owner: Antigravity | Status: Deferred (out of scope for current demo round)
- [ ] RTL-T6 | stretch: IK-solver | Owner: Antigravity | Status: Deferred (out of scope for current demo round)
- [ ] RTL-T7 | pre-synthesized bitstreams | Owner: Antigravity | Status: Deferred (out of scope for current demo round)

## TRACK MBD — Model-Based Design Engine
- [x] MBD-T1  | node vocabulary | Owner: Codex | Status: Complete
- [x] MBD-T1a | pin config screen | Owner: Antigravity | Status: Complete (standalone pin_config.html + Express server; board→real pins from boards.yaml; drag-drop to SensorInput/ActuatorOutput slots; conflict detection)
- [x] MBD-T1b | pin→codegen wire | Owner: Antigravity | Status: Complete (POST /api/generate injects HYP_PIN_* #defines into hyp_board_config.h; tested: assign SPI1MOSI → header reflects it)
- [x] MBD-T1c | pin assignment presets | Owner: Antigravity | Status: Complete (Quick Select preset panel on pin-config UI: SPI Accelerometer, RS-485 Modbus, CORDIC FPGA Link, Servo/PWM; validated per board pinout against boards.yaml with conflict detection)
- [x] MBD-T1d | Hardware signal model & validation | Owner: Antigravity | Status: Complete (Canonical semantic roles SPI [sck, mosi, miso, cs], UART [tx, rx], I2C [sda, scl], PWM [output], ADC [input], GPIO [gpio]; hardwareResourceId schema constraints; legacy signal-role migration; generic peripheral resolution; C macro sanitization; validation rules for unknown resources, invalid signal roles, duplicate pins, and unconfigured resources; verified by scratch/test_hardware_model.js)
- [x] MBD-T2  | graph data model | Owner: Codex | Status: Complete
- [x] MBD-T3  | graph→C codegen | Owner: Codex | Status: Complete (Generates `hyp_graph_<id>_step()` in C for CordicOp and Publish nodes)
- [x] MBD-T4  | HW backend routing | Owner: Codex | Status: Complete
- [x] MBD-T5  | editor UI | Owner: Codex | Status: Complete (React Flow graph view at `/graph`; fixed node vocabulary, drag/connect canvas, parameter panels, CordicOp hardware/software routing, embedded pin-config view)
- [x] MBD-T5a | Graph editor UX enhancements | Owner: Antigravity | Status: Complete (Delete/Backspace node deletion, right-click context menu, delete confirmation dialog, Undo/Redo [Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y], history stack for creation/deletion/movement/edge/params/clear, restored nodes/edges, IIFE protection in graph_editor.html)
- [x] MBD-T6  | editor→codegen wire | Owner: Codex | Status: Complete (`Build` POSTs graph to `/api/build`, invokes `mbd/codegen/graph_to_c.js`, displays generated C)
- [x] MBD-T7  | complexity comparison | Owner: Gemini CLI | Status: Complete (Comparison document created in mbd/docs/complexity_comparison.md, committed in ce162bc)
- [x] MBD-T8  | lineages comparison | Owner: Gemini CLI | Status: Complete (Comparison document created in mbd/docs/three_lineages_comparison.md, committed in ce162bc)
- [/] MBD-T9  | live reflash demo | Owner: Hermes / Nemotron | Status: Partial — PlatformIO ESP32 target project (`mbd/esp32`), generated-source materialization, server compile/flash/verify endpoints (`/api/compile`, `/api/flash`, `/api/verify`), serial-port detection (`/api/esp32/ports`), captured command logs, and UI actions implemented. **ESP32 target compilation verified via `/api/compile`**. Physical compile/flash/runtime verification remains Blocked on local PlatformIO environment installation and connected CP2102 hardware device.
- [ ] MBD-T10 | pre-synth bitstreams | Owner: Antigravity | Status: Deferred (out of scope for current demo round)

### CRITICAL CODEGEN GAP
- [x] MBD-GAP1 | SensorInput & ActuatorOutput peripheral C codegen | Owner: Hermes / Nemotron | Status: Complete (Implemented `hyp_sensor_read()` and `hyp_actuator_write()` SDK primitives; ESP32 backend in `hyp_esp32_hw.cpp`; `graph_to_c.js` codegen for SensorInput/ActuatorOutput; verified host compilation and syntax check)

## TRACK FW — Firmware / Target Backend

### A. Firmware Infrastructure
- [/] FW-T1 | ESP32 target backend & initialization | Owner: Antigravity | Status: Partial (`sdk/src/hyp_esp32_hw.cpp` / `sdk/include/hyp_esp32_hw.h` provide target initialization stubs (`hyp_esp32_hw_init`))
- [x] FW-T2 | Board configuration consumption | Owner: Antigravity | Status: Complete (`gen_board_config.js` extracts pin assignments from `hardware.json` and injects `#define HYP_PIN_*` into `hyp_board_config.h`)
- [x] FW-T3 | Target startup & initialization contract | Owner: Antigravity | Status: Complete (Runtime markers defined in `mbd/esp32/README.md` & Arduino wrapper: `HYPRACCEL_MBD_T9_READY`, `HYPRACCEL_GRAPH_ID=...`, `HYP_PUBLISH topic=...`)
- [/] FW-T4 | SDK target implementation & drivers | Owner: Antigravity | Status: Partial (C SDK targets ESP32 and THEJAS RISC-V host baselines; hardware driver generation pending)

### B. Target Peripheral Implementation
- [x] FW-P1 | Target GPIO initialization & support | Owner: Antigravity/Sonnet | Status: Complete (GPIO init blocks consume `HYP_RESOURCE_GPIO_GPIOx` and `HYP_RESOURCE_GPIO_GPIOx_DIRECTION/_PULL/_INITIAL_STATE` macros; runtime `hyp_esp32_sensor_read`/`hyp_esp32_actuator_write` resolve pin via macro-driven `hyp_resource_pin_table`; no hardcoded pin numbers; verified by `test_board_config_consumption` 29/29)
- [x] FW-P2 | Target UART initialization & support | Owner: Antigravity/Sonnet | Status: Complete (UART1/UART2 `Serial1/Serial2.begin()` now consume `HYP_RESOURCE_UART_UARTx_TX_PIN`/`_RX_PIN` from generated board config; hardcoded `UART1_TX=10` etc. eliminated; `#error` guard on missing macros; sensor_read routes to correct Serial port via resource ID)
- [x] FW-P3 | Target SPI initialization & support | Owner: Antigravity/Sonnet | Status: Complete (HSPI/VSPI `begin()` calls now consume `HYP_RESOURCE_SPI_HSPI_SCK_PIN`/`_MOSI_PIN`/`_MISO_PIN`/`_CS_PIN` from generated config; `#error` guard on missing macros; hardcoded `14,12,13,15` constants eliminated; gen_board_config.js emits full SPI pin macros from boards.yaml; verified 8/8 pin values)
- [x] FW-P4 | Target I2C initialization & support | Owner: Antigravity/Sonnet | Status: Complete (`Wire.begin()` now consumes `HYP_RESOURCE_I2C_I2C0_SDA_PIN`/`_SCL_PIN` from generated config; hardcoded `21,22` eliminated; `#error` guard; gen_board_config.js emits I2C pin macros; verified 2/2 pin values)
- [x] FW-P5 | Target PWM initialization & support | Owner: Antigravity/Sonnet | Status: Complete (PWM init blocks consume `HYP_RESOURCE_PWM_GPIOx` macros; `ledcAttach`/`ledcWrite` use pin from macro; runtime `hyp_esp32_actuator_write` resolves duty-cycle via macro-driven table; gen_board_config.js emits `HYP_RESOURCE_PWM_GPIOx_PIN` for all capable GPIO; verified 3 PWM pins)
- [x] FW-P6 | Target ADC initialization & support | Owner: Antigravity/Sonnet | Status: Complete (ADC init blocks consume `HYP_RESOURCE_ADC_GPIOx` macros; `analogRead()` pin resolved at runtime from macro-driven `hyp_resource_pin_table`; gen_board_config.js emits `HYP_RESOURCE_ADC_GPIOx_PIN` for all ADC-capable GPIO; verified 3 ADC pin values)

### C. Generated Firmware Integration
- [x] FW-G1 | Generated source & wrapper materialization | Owner: Antigravity | Status: Complete (Express server materializes generated `graph.c`, SDK sources, and Arduino wrapper into `mbd/esp32/generated/`)
- [x] FW-G2 | Target build system integration | Owner: Hermes / Nemotron | Status: Complete (PlatformIO `platformio.ini` & `/api/compile` endpoint implemented; **host CLI environment verified — ESP32 compilation successful**)

### D. Physical Deployment & Verification
- [ ] FW-D1 | Physical hardware flashing workflow | Owner: Future | Status: Not started (Blocked on physical CP2102 serial target hardware and host PlatformIO CLI)
- [ ] FW-D2 | Physical target runtime verification | Owner: Future | Status: Not started (Blocked on physical flash execution and serial verification)

## TRACK SDK — Runtime SDK
The runtime SDK provides embedded primitives required by generated C step functions and block library nodes.

#### Implemented Core Primitives
- **`CORE-T1..T7` | Core SDK Engine**: `Complete`. `hyp_cordic_ref()`, `hyp_cordic_rtl()`, `hyp_router_dispatch()`, and `hyp_publish()` are complete and verified.

#### Runtime Primitives (Specification & Implementation Status)
- **`SDK-T1` | `hyp_sensor_read`**: `Complete` (Owner: Antigravity/Sonnet — Public contract, canonical `hardwareResourceId` resolution, GPIO/ADC/UART ESP32 reads, and explicit SPI/I2C unsupported handling; test in `sdk/test/test_sensor_actuator.c`)
- **`SDK-T2` | `hyp_actuator_write`**: `Complete` (Owner: Antigravity/Sonnet — Public contract, canonical `hardwareResourceId` resolution, GPIO/PWM/UART ESP32 writes, and explicit SPI/I2C unsupported handling; test in `sdk/test/test_sensor_actuator.c`)
- **`SDK-T3` | `hyp_pid_step` & PID State**: `Not started` (Specification stage). PID control loop calculation with anti-windup and state structures.
- **`SDK-T4` | Kinematics Runtime**: `Not started` (Specification stage). Forward/Inverse kinematics matrix and vector routines.
- **`SDK-T5` | Encoder Runtime**: `Not started` (Specification stage). Quadrature encoder pulse counting and velocity estimation routines.
- **`SDK-T6` | Quaternion & SE(3) Runtime**: `Not started` (Specification stage). 3D orientation math (quaternions, Euler angles, SE(3) transformation matrices).
- **`SDK-T7` | Joint Controller Runtime**: `Not started` (Specification stage). Multi-axis joint position/velocity trajectory controller primitive.
- **`SDK-T8` | Custom-Node Runtime Support**: `Not started` (Specification stage). Extension hooks for embedding user C functions into the graph execution loop.

## TRACK NODE LIBRARY — NL
*(Note: Implementing any node requires ALL of: 1. graph schema, 2. editor palette/representation, 3. ports and type definitions, 4. parameter validation, 5. graph-to-C code generation, 6. SDK/runtime dependency, 7. target/backend support, 8. tests. Node Catalog ≠ Node Implementation.)*
- [x] NL-P0 | Core P0 node implementation | Owner: Hermes / Nemotron | Status: Complete (`CordicOp`, `Publish`, `SensorInput`, `ActuatorOutput` all fully implemented with schema, palette, ports, validation, codegen, SDK, backend, and tests)
- [ ] NL-P1 | Basic math & signal processing nodes | Owner: Future | Status: Not started
- [ ] NL-P2 | Control nodes (PID, PI, Lead-Lag) | Owner: Future | Status: Not started
- [ ] NL-R1 | 3-DOF robotics foundation nodes | Owner: Future | Status: Not started
- [ ] NL-4WD | 4WD / mobile robotics nodes | Owner: Future | Status: Not started
- [ ] NL-R2 | 6-DOF robotics foundation nodes | Owner: Future | Status: Not started
- [ ] NL-COM | Communication & timing nodes | Owner: Future | Status: Not started
- [/] NL-DATA | Data & telemetry nodes | Owner: Antigravity | Status: Partial (`Publish` topic streaming complete; logging pending)
- [ ] NL-CUSTOM | Custom code & function nodes | Owner: Future | Status: Not started
- [ ] NL-ADV | Advanced P2/P3 nodes | Owner: Future | Status: Not started

## TRACK CODEGEN — CG
- [x] CG-T1 | Expand graph schema for node library | Owner: Codex / Antigravity | Status: Complete
- [x] CG-T2 | Node parameter & port validation | Owner: Antigravity | Status: Complete
- [x] CG-T3 | Node → C code generation engine | Owner: Hermes / Nemotron | Status: Complete (`graph_to_c.js` supports `CordicOp`, `Publish`, `SensorInput`, `ActuatorOutput` with full hardware resource mapping)
- [x] CG-T4 | SDK dependency resolution & includes | Owner: Antigravity | Status: Complete
- [x] CG-T5 | Generated source, header, & board config management | Owner: Antigravity | Status: Complete
- [x] CG-T6 | Generated firmware project integration | Owner: Antigravity | Status: Complete
- [x] CG-T7 | Generated project build verification | Owner: Hermes / Nemotron | Status: Complete (Host SDK unit tests pass; **physical PlatformIO target build verification successful via `/api/compile`**)
- [/] CG-T8 | Codegen error reporting & diagnostics | Owner: Antigravity | Status: Partial

## TRACK VALIDATION — MBD Validation Lifecycle (VAL)
- [ ] VAL-T1 | Model-in-the-Loop (MIL) | Owner: Future | Status: Not started (Future Roadmap — pure host-side model simulation)
- [x] VAL-T2 | Software-in-the-Loop (SIL) | Owner: Antigravity | Status: Complete (Host C reference & Verilator host-side tests pass)
- [ ] VAL-T3 | Processor-in-the-Loop (PIL) | Owner: Future | Status: Not started (Future Roadmap — target execution via debug co-simulation)
- [ ] VAL-T4 | Hardware-in-the-Loop (HIL) | Owner: Future | Status: Not started (Future Roadmap — target execution against plant stand)
- [/] VAL-T5 | Physical / Field validation | Owner: Antigravity | Status: Partial (`PLAT-T1` dashboard & `/api/verify` complete; physical test stand pending)

## TRACK SCHEMATIC — SCH (Schematic Integration)
- [x] SCH-T1 | KiCad .kicad_sch import & parser | Owner: Antigravity / Gemini 3.6 Flash | Status: Complete (Deterministic S-expression tokenizer & AST parser for KiCad 6/7/8 .kicad_sch files; extracts metadata, symbols, pins, wires, labels, nets, and handles malformed schematics)
- [x] SCH-T2 | Symbol & net extraction engine | Owner: Antigravity / Gemini 3.6 Flash | Status: Complete (Normalized Symbol, Pin, Net internal representation with coordinate graph connectivity resolution & MCU pin -> net -> external pin trace)
- [x] SCH-T3 | MCU pin/peripheral resolution & signal mapping | Owner: Antigravity / Gemini 3.6 Flash | Status: Complete (Target MCU symbol resolution against boards.yaml for esp32 & thejas32; structured unresolved target handling for unknown/ambiguous MCUs)
- [x] SCH-T4 | Schematic → Hardware Resource Model generation | Owner: Antigravity / Gemini 3.6 Flash | Status: Complete (Maps schematic pin/net roles to canonical SPI, UART, I2C, PWM, ADC, GPIO semantic roles and outputs projectHardware resource model; verified by scratch/test_schematic_import.js)
- [ ] SCH-T5 | Conflict & unconnected resource detection | Owner: Future | Status: Not started (Future Roadmap)
- [ ] SCH-T6 | Schematic ↔ Hardware Setup consistency validator | Owner: Future | Status: Not started (Future Roadmap)
- [/] SCH-T7 | MBD Node → schematic resource binding | Owner: Antigravity | Status: Partial (Graph schema & UI binding complete; schematic parser side Future Roadmap)
- [/] SCH-T8 | EasyEDA .tel netlist import | Owner: Codex | Status: Partial (EasyEDA `.tel` adapter and ESP32 physical pin recovery added; real fixture `Netlist_Schematic1_2026-08-18.tel` is not present in this workspace, so full fixture-backed verification is pending)

## TRACK SIM — Simulation Infrastructure & Plant Models
- [ ] SIM-T1 | Gazebo 6-DOF arm URDF | Owner: Antigravity | Status: Not started
- [ ] SIM-T2 | Kinematics definition | Owner: Antigravity | Status: Not started
- [ ] SIM-T3 | Bridge to Gazebo | Owner: Antigravity | Status: Not started
- [ ] SIM-T4 | HW CORDIC in bridge | Owner: Antigravity | Status: Not started
- [ ] SIM-T5 | Surgical framing | Owner: Antigravity | Status: Deferred
- [ ] SIM-T6 | N-point stress test | Owner: Antigravity | Status: Deferred

## TRACK PLATFORM — Telemetry & Tooling
- [x] PLAT-T1 | Telemetry dashboard | Owner: Antigravity | Status: Complete
- [x] PLAT-T2 | Siemens comparison | Owner: Gemini CLI | Status: Complete
- [ ] PLAT-T3 | Bare carrier PCB | Owner: Antigravity | Status: Deferred

## TRACK HW-UX — Hardware Setup UX v2
*Evolve the current flat pin-configuration UI into a structured, resource-oriented engineering workflow. Reuses the canonical hardware resource model (MBD-T1d) and schematic import backend (SCH-T1..T4).*

- [ ] HW-UX-T1 | Hardware Setup v2 foundation / resource grouping (Board → Peripheral → Semantic Signals → Physical Pins) | Owner: Antigravity | Status: Not started
- [ ] HW-UX-T2 | Peripheral / semantic signal sections (GPIO, SPI, I2C, UART, PWM, ADC grouped under resources) | Owner: Antigravity | Status: Not started
- [ ] HW-UX-T3 | Project target board selector integration (single project-level board state shared by Hardware Setup, Graph Editor, Codegen, Build/Deploy) | Owner: Antigravity | Status: Not started
- [ ] HW-UX-T4 | KiCad schematic import UI (expose SCH-T1..T4 importer via Hardware Setup: "Manual Configuration" OR "Import KiCad Schematic") | Owner: Antigravity | Status: Not started
- [ ] HW-UX-T5 | Schematic import preview and apply workflow (detected MCU, resources, signals, resolved pins, warnings/errors) | Owner: Antigravity | Status: Not started
- [ ] HW-UX-T6 | Pinout visualization / CubeMX-style board view (physical pins, assigned peripherals, semantic roles, conflicts, available pins) | Owner: Antigravity | Status: Not started
- [ ] HW-UX-T7 | Hardware Setup ↔ Graph Editor integration polish (shared canonical hardware state; graph references resources, not independent pin mappings) | Owner: Antigravity | Status: Not started
- [ ] HW-UX-T8 | Hardware Setup validation/error UX (surface existing canonical validation: duplicate pins, invalid roles, invalid combinations, unknown resources, unconfigured resources, unresolved schematic MCU, conflicting assignments) | Owner: Antigravity | Status: Not started
