Golden `hyp_board_config.h` outputs captured **before** clock.json / desktop
codegen support was added (web-editor-only projects, no clock.json).
Regenerate only deliberately: `node capture.js <outDir>` from `mbd/editor`.

- `cli_<board>.h`                — `gen_board_config.js <board> boards.yaml <dir>`
- `materialize_<project>.h`      — `POST /api/projects/<id>/generate` → generated/hyp_board_config.h
- `api_generate_<project>.h`     — `POST /api/generate?projectId=<id>` → boards/codegen/hyp_board_config.h
Projects: `demo_project` (copy of .hypraccel/projects/demo_project) and
`web_uart` (UART2 tx/rx with baudRate 57600 + PWM on GPIO25, see capture.js).
