# Hardware Setup model (v1)

Hardware Setup extends the existing board descriptor; it is not a second board
database. `boards/boards.yaml` remains the target backend source of truth for
available GPIO and peripheral instances, their signal roles, and their valid
default pin mappings.

The project stores only the selected board and the user's configuration in
`.hypraccel/hardware.json` (a generated, ignored project-state file):

- `board`: board descriptor key.
- `resources`: generic resource identities (`spi.vspi`, `uart.uart2`,
  `i2c.i2c0`, `pwm.GPIO25`, `adc.GPIO34`, `gpio.GPIO4`), availability, valid
  assignments copied from the selected board, and user configuration.
- `assignments`: authoritative physical mappings: node label, logical role,
  pin, and resource identity.

The model has no ESP32 behaviour. ESP32 capability and pin knowledge lives in
the `esp32` descriptor in `boards/boards.yaml`. The server validates a project
assignment against that descriptor, then the existing board-header generation
flow emits resource and pin macros into `hyp_board_config.h`.

Graph `SensorInput` and `ActuatorOutput` nodes contain only an optional
`hardwareResource` reference. They do not contain GPIO pin lists or peripheral
configuration. The generated ESP32 project copies the existing generated board
header, so hardware configuration is consumed before graph application C.

## User-defined device/profile extension point (HW-UX-T7)

The project hardware document may also contain a `devices` array. This is a
deliberately small profile layer, not a node library or device database:

```json
{
  "id": "LCD1",
  "name": "16x2 LCD",
  "profile": "lcd-16x2",
  "connections": [
    {"name": "RS", "resource": "gpio.GPIO16", "pin": "GPIO16"},
    {"name": "E",  "resource": "gpio.GPIO17", "pin": "GPIO17"}
  ]
}
```

The server validates profile IDs, unique named connections, canonical
resource IDs, and pin compatibility against the selected board descriptor.
Profiles are preserved by Hardware Setup but have no arbitrary-device UI or
code generation yet. Future device nodes/codegen can reference
`device.<id>` and resolve named connections through this project-owned layer.
This keeps semantic device wiring separate from the existing generic
`SensorInput`/`ActuatorOutput` assignments without changing schematic import:
SCH-T1..SCH-T8 continue to produce canonical board resource assignments, and
profiles can be layered on top later.
