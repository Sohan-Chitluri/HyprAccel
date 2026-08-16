# HyprAccel Task Progress
*Note: Owner field = the AI tool/person actually executing this task, update on every status change.*

## TRACK CORE
- [x] CORE-T1 | API header | Owner: Antigravity | Status: Complete
- [x] CORE-T2 | boards.yaml schema | Owner: Antigravity | Status: Complete
- [x] CORE-T3 | yaml→header codegen | Owner: Antigravity | Status: Complete
- [x] CORE-T4 | CORDIC ref C impl | Owner: Antigravity | Status: Complete (Fixed Q1.15 conversion factor and angle table, validated < 2e-4 error)
- [x] CORE-T5 | THEJAS sw path | Owner: Antigravity | Status: Complete
- [x] CORE-T6 | ESP32 sw path | Owner: Antigravity | Status: Complete
- [x] CORE-T7 | routing layer | Owner: Antigravity | Status: Complete

## TRACK RTL
- [ ] RTL-T1 | validate existing RTL | Owner:  | Status: Blocked on Q2 (waiting for RTL location)
- [ ] RTL-T2 | register interface | Owner:  | Status: Not started (Decision: SPI for THEJAS<->FPGA)
- [ ] RTL-T3 | synth + timing | Owner:  | Status: Blocked on Q4 (waiting for target FPGA board)
- [ ] RTL-T4 | host-side driver | Owner:  | Status: Not started
- [ ] RTL-T5 | latency measurement | Owner:  | Status: Not started
- [ ] RTL-T6 | stretch: IK-solver | Owner:  | Status: Not started
- [ ] RTL-T7 | pre-synthesized bitstreams | Owner:  | Status: Not started

## TRACK MBD
- [x] MBD-T1  | node vocabulary | Owner: Codex | Status: Complete
- [ ] MBD-T1a | pin config screen | Owner:  | Status: Not started
- [ ] MBD-T1b | pin→codegen wire | Owner:  | Status: Not started
- [x] MBD-T2  | graph data model | Owner: Codex | Status: Complete
- [ ] MBD-T3  | graph→C codegen | Owner:  | Status: Not started
- [ ] MBD-T4  | HW backend routing | Owner:  | Status: Not started
- [ ] MBD-T5  | editor UI | Owner:  | Status: Not started (Decision: local dev server)
- [ ] MBD-T6  | editor→codegen wire | Owner:  | Status: Not started
- [ ] MBD-T7  | complexity comparison | Owner:  | Status: Not started
- [x] MBD-T8  | lineages comparison | Owner: Gemini CLI | Status: Complete (Comparison document created in mbd/docs/three_lineages_comparison.md)
- [ ] MBD-T9  | live reflash demo | Owner:  | Status: Not started
- [ ] MBD-T10 | pre-synth bitstreams | Owner:  | Status: Not started

## TRACK SIM
- [ ] SIM-T1 | Gazebo 6-DOF arm | Owner:  | Status: Not started
- [ ] SIM-T2 | kinematics definition | Owner:  | Status: Not started
- [ ] SIM-T3 | bridge to Gazebo | Owner:  | Status: Not started
- [ ] SIM-T4 | HW CORDIC in bridge | Owner:  | Status: Not started
- [ ] SIM-T5 | surgical framing | Owner:  | Status: Not started
- [ ] SIM-T6 | N-point stress test | Owner:  | Status: Not started

## TRACK PLATFORM
- [ ] PLAT-T1 | telemetry dashboard | Owner:  | Status: Not started
- [x] PLAT-T2 | Siemens comparison | Owner: Gemini CLI | Status: Complete
- [ ] PLAT-T3 | Bare carrier PCB | Owner:  | Status: Not started
