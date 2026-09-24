# HyprAccel native desktop

First C++17 / Qt 6 Widgets slice. Lovable Dev Studio is the visual target; CubeMX informs hardware features and Simulink informs MBD behavior. This is not yet a complete visual replica.

## Build and run (NixOS)

From the repository root:

    nix-shell desktop/shell.nix --run 'cmake -S desktop -B desktop/build -G Ninja && cmake --build desktop/build'
    nix-shell desktop/shell.nix --run 'QT_QPA_PLATFORM=xcb desktop/build/hypraccel-studio'

On other systems install Qt 6 Widgets development files, CMake and a C++17 compiler, then use the same CMake commands directly.

## Verification

    nix-shell desktop/shell.nix --run 'ctest --test-dir desktop/build --output-on-failure'
    nix-shell desktop/shell.nix --run 'QT_QPA_PLATFORM=offscreen desktop/build/hypraccel-studio --smoke-test --screenshots'

The smoke test checks opening the unsaved workspace, switching all five tabs, and returning to the launcher. Screenshots go into desktop/build/.

Implemented: Lovable-palette Qt theme module, native dark launcher, workspace navigation, resizable three-pane layouts, New/Save/Open/Close/Quit actions, real board selector (thejas32, esp32) and an editable logical pinout from boards.yaml with a colour-coded legend. Right-clicking a pin opens a function menu (GPIO/PWM/ADC/UART/SPI/I2C) backed by a shared PinAssignmentModel; the Pin Inspector and assigned-pin count update live. Clock Configuration renders each board's clock tree with editable mux/PLL/divider controls and live frequency/limit checks. File > Save Project / Open Project persist pin assignments and clock selections into the same project store the web editor (mbd/editor) uses (`hardware/hardware.json`, `hardware/clock.json`, `project.json`), so a project saved here opens unchanged there. Import remains disabled. The MBD tab edits the project's graphs (`graphs/<id>.json`) in the same format the web editor saves, byte for byte: a node palette and inspector driven by `src/mbd/nodes/node_types.json` (kept in sync with `mbd/editor/src/graph_editor.html` by the `node_types_drift` test), a canvas with the web's connection rules plus editor-time refusal of edges codegen always rejects, and Generate C, which saves and runs `mbd/codegen/graph_to_c.js` exactly as the web server's `/api/build` does. See `docs/mbd_graph_contract.md`. The Firmware tab is still a placeholder. Existing backend and firmware sources are untouched.

Next: reproduce Lovable's board selector and pinout screen faithfully, bind repository board descriptors, then full firmware materialization from desktop (SDK copy, board header, `main.cpp`, `platformio.ini`), which the web does in `materializeEsp32`.

## Known gaps

Desktop Generate does not yet check for a stale target board or unconfigured hardware resources, which the web server refuses before codegen.
