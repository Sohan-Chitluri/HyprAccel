# Contributing to HyprAccel

## Repository Setup
1. Clone the repository.
2. Ensure you have `Node.js` (for the MBD editor/server), `PlatformIO` (for embedded compilation), and `Verilator` (if contributing to the RTL tests).
3. Install dependencies in `mbd/editor`: `cd mbd/editor && npm install`.

## Development Workflow
- **MBD Editor**: Modifications to the visual graph editor should maintain the clean split between the UI components and the `server.js` backend API.
- **Code Generation**: The generator (`mbd/codegen/`) must strictly abide by the graph schema contracts.
- **SDK**: Runtime primitives (`sdk/`) must be platform-agnostic unless housed within target-specific wrappers (e.g., `hyp_esp32_hw.cpp`).
- **Graph Nodes**: Any new nodes must be registered in the central schema (`mbd/schema/`) and appropriately handled by the C code generator. All new functionality should be validated visually in the graph editor.

## Testing
Always validate your changes with the existing test suites:
```bash
npm --prefix mbd/editor run test:projects
```
For hardware graph testing, generate the project files via the UI and verify successful compilation through PlatformIO.

## Coding Expectations
- Maintain existing C/C++ coding styles (e.g., prefixing `hyp_`).
- Avoid adding heavy node modules or Python dependencies unless absolutely required.
- Keep contributions small, documented, and properly tested.
