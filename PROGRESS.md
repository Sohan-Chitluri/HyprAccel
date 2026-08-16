# HyprAccel Task Progress
*Note: Owner field = the AI tool/person actually executing this task, update on every status change.*

## TRACK CORE
- [x] CORE-T1 | API header | Owner: Antigravity | Status: Complete
- [x] CORE-T2 | boards.yaml schema | Owner: Antigravity | Status: Complete
- [x] CORE-T3 | yaml→header codegen | Owner: Antigravity | Status: Complete
- [x] CORE-T4 | CORDIC ref C impl | Owner: Antigravity | Status: Complete (Revised to match confirmed RTL parameters: 8-stage Q4.12, bit-exact verified against 10k vectors)
- [x] CORE-T5 | THEJAS sw path | Owner: Antigravity | Status: Complete
- [x] CORE-T6 | ESP32 sw path | Owner: Antigravity | Status: Complete
- [x] CORE-T7 | routing layer | Owner: Antigravity | Status: Complete

## TRACK RTL
- [ ] RTL-T1 | validate existing RTL | Owner: Codex | Status: Blocked — copied 8-stage Q4.12 RTL and ran the exact 10,000-vector Verilator regression; all 10,000 vectors disagree with reference/cordic_golden/cordic_golden.py (no tolerance applied). Example: x=-5561, y=-4453, z=-5744, sat=0: RTL=(-5368, 4686, 5, 0), golden=(-5294, 4769, 26, 0). Reconcile the source RTL/golden arithmetic contract before RTL-T1 can complete.
- [ ] RTL-T2 | register interface | Owner:  | Status: Not started (Decision: SPI for THEJAS<->FPGA)
- [ ] RTL-T3 | synth + timing | Owner:  | Status: Blocked on Q4 (waiting for target FPGA board)
- [ ] RTL-T4 | host-side driver | Owner:  | Status: Not started
- [ ] RTL-T5 | latency measurement | Owner:  | Status: Not started
- [ ] RTL-T6 | stretch: IK-solver | Owner:  | Status: Not started
- [ ] RTL-T7 | pre-synthesized bitstreams | Owner:  | Status: Not started

## TRACK MBD
- [x] MBD-T1  | node vocabulary | Owner: Codex | Status: Complete
- [x] MBD-T1a | pin config screen | Owner: Antigravity (Sonnet) | Status: Complete (standalone pin_config.html + Express server; board→real pins from boards.yaml; drag-drop to SensorInput/ActuatorOutput slots; conflict detection)
- [x] MBD-T1b | pin→codegen wire | Owner: Antigravity (Sonnet) | Status: Complete (POST /api/generate injects HYP_PIN_* #defines into hyp_board_config.h; tested: assign SPI1MOSI → header reflects it)
- [x] MBD-T2  | graph data model | Owner: Codex | Status: Complete
- [x] MBD-T3  | graph→C codegen | Owner: Codex (GPT-5.6 Terra) | Status: Complete
- [x] MBD-T4  | HW backend routing | Owner: Codex (GPT-5.6 Terra) | Status: Complete
- [x] MBD-T5  | editor UI | Owner: Codex (GPT-5.6 Terra) | Status: Complete (React Flow graph view at `/graph` on the existing port-3737 Express server; fixed node vocabulary, drag/connect canvas, parameter panels, CordicOp hardware/software routing, and embedded existing pin-config view for SensorInput/ActuatorOutput)
- [x] MBD-T6  | editor→codegen wire | Owner: Codex (GPT-5.6 Terra) | Status: Complete (`Build` POSTs the current graph to `/api/build`, invokes `mbd/codegen/graph_to_c.js`, and displays generated C; verified against the committed CordicOp→Publish example)
- [x] MBD-T7  | complexity comparison | Owner: Gemini CLI | Status: Complete (Comparison document created in mbd/docs/complexity_comparison.md) ⚠️ AUDIT: file exists on disk but has NO git commit — untracked, at risk of loss
- [x] MBD-T8  | lineages comparison | Owner: Gemini CLI | Status: Complete (Comparison document created in mbd/docs/three_lineages_comparison.md) ⚠️ AUDIT: file exists on disk but has NO git commit — untracked, at risk of loss
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
- [x] PLAT-T1 | telemetry dashboard | Owner: Antigravity | Status: Complete ⚠️ AUDIT: files exist in plat/dashboard/ but have NO git commit — untracked, at risk of loss
- [x] PLAT-T2 | Siemens comparison | Owner: Gemini CLI | Status: Complete ⚠️ AUDIT: file exists in plat/docs/ but has NO git commit — untracked, at risk of loss
- [ ] PLAT-T3 | Bare carrier PCB | Owner:  | Status: Not started
