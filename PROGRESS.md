# HyprAccel Task Progress

## TRACK CORE
- [ ] CORE-T1 | API header | Owner: Antigravity | Status: Not started
- [ ] CORE-T2 | boards.yaml schema | Owner: Antigravity | Status: Not started
- [ ] CORE-T3 | yaml→header codegen | Owner: Antigravity | Status: Not started
- [x] CORE-T4 | CORDIC ref C impl | Owner: Antigravity | Status: Complete (Fixed Q1.15 conversion factor and angle table, validated < 2e-4 error)
- [ ] CORE-T5 | THEJAS sw path | Owner: Antigravity | Status: Not started
- [ ] CORE-T6 | ESP32 sw path | Owner: Antigravity | Status: Not started
- [ ] CORE-T7 | routing layer | Owner: Antigravity | Status: Not started

## TRACK RTL
- [ ] RTL-T1 | validate existing RTL | Owner: Antigravity | Status: Blocked on Q2 (waiting for RTL location)
- [ ] RTL-T2 | register interface | Owner: Antigravity | Status: Not started (Decision: SPI for THEJAS<->FPGA)
- [ ] RTL-T3 | synth + timing | Owner: Antigravity | Status: Blocked on Q4 (waiting for target FPGA board)
- [ ] RTL-T4 | host-side driver | Owner: Antigravity | Status: Not started
- [ ] RTL-T5 | latency measurement | Owner: Antigravity | Status: Not started
- [ ] RTL-T6 | stretch: IK-solver | Owner: Antigravity | Status: Not started
- [ ] RTL-T7 | pre-synthesized bitstreams | Owner: Antigravity | Status: Not started

## TRACK MBD
- [x] MBD-T1  | node vocabulary | Owner: Codex | Status: Complete
- [ ] MBD-T1a | pin config screen | Owner: Antigravity | Status: Not started
- [ ] MBD-T1b | pin→codegen wire | Owner: Antigravity | Status: Not started
- [x] MBD-T2  | graph data model | Owner: Codex | Status: Complete
- [ ] MBD-T3  | graph→C codegen | Owner: Antigravity | Status: Not started
- [ ] MBD-T4  | HW backend routing | Owner: Antigravity | Status: Not started
- [ ] MBD-T5  | editor UI | Owner: Antigravity | Status: Not started (Decision: local dev server)
- [ ] MBD-T6  | editor→codegen wire | Owner: Antigravity | Status: Not started
- [ ] MBD-T7  | complexity comparison | Owner: Antigravity | Status: Not started
- [x] MBD-T8  | lineages comparison | Owner: Gemini CLI | Status: Complete (Three-lineage comparison document created in mbd/docs/three_lineages_comparison.md)
- [ ] MBD-T9  | live reflash demo | Owner: Antigravity | Status: Not started
- [ ] MBD-T10 | pre-synth bitstreams | Owner: Antigravity | Status: Not started

## TRACK SIM
- [ ] SIM-T1 | Gazebo 6-DOF arm | Owner: Antigravity | Status: Not started
- [ ] SIM-T2 | kinematics definition | Owner: Antigravity | Status: Not started
- [ ] SIM-T3 | bridge to Gazebo | Owner: Antigravity | Status: Not started
- [ ] SIM-T4 | HW CORDIC in bridge | Owner: Antigravity | Status: Not started
- [ ] SIM-T5 | surgical framing | Owner: Antigravity | Status: Not started
- [ ] SIM-T6 | N-point stress test | Owner: Antigravity | Status: Not started

## TRACK PLATFORM
- [ ] PLAT-T1 | telemetry dashboard | Owner: Antigravity | Status: Not started
- [ ] PLAT-T2 | Siemens comparison | Owner: Antigravity | Status: Not started
- [ ] PLAT-T3 | Bare carrier PCB | Owner: Antigravity | Status: Not started
