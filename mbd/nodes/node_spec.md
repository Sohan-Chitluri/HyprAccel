# HyprAccel MBD V1 node vocabulary

This document is the fixed node vocabulary for the V1 model-based design (MBD)
editor. A graph node has an `id`, a `type`, and a `params` object. Port names
are part of this contract: edges refer to them by name and may only join ports
with the same value type. Nodes execute in dependency order; a graph input may
remain unconnected when the host supplies it as an external input.

## Shared value types

| Type | JSON representation | Meaning |
| --- | --- | --- |
| `number` | JSON number | Scalar engineering value, normally SI units. |
| `boolean` | JSON boolean | Logical state. |
| `vector<number>` | JSON number array | Ordered numeric vector. |
| `pose` | object | `{ "position_m": [x, y, z], "orientation_rad": [roll, pitch, yaw] }`. |
| `any` | any JSON value | Payload forwarded without numeric conversion. |

## `SensorInput`

Imports one value from a named board, simulator, or host-provided signal.

| Direction | Port | Type | Description |
| --- | --- | --- |
| Output | `value` | configured `valueType` | Latest sampled signal value. |
| Output | `timestamp_us` | `number` | Timestamp assigned by the source, in microseconds. |
| Output | `valid` | `boolean` | `true` when `value` is usable. |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `source` | string | yes | Stable source identifier, such as `imu.yaw_rad`. |
| `valueType` | `number` \| `boolean` \| `vector<number>` \| `pose` \| `any` | yes | Type emitted from `value`. |
| `samplePeriodUs` | positive integer | yes | Nominal sampling period in microseconds. |
| `unit` | string | no | Engineering unit label, for example `rad` or `m/s`. |

## `CordicOp`

Performs a CORDIC-compatible trigonometric operation. Its configuration chooses
the operation and therefore its port set.

| `operation` | Inputs | Outputs |
| --- | --- | --- |
| `sin` | `angle_rad: number` | `value: number` (sine) |
| `cos` | `angle_rad: number` | `value: number` (cosine) |
| `sincos` | `angle_rad: number` | `sin: number`, `cos: number` |
| `atan2` | `y: number`, `x: number` | `angle_rad: number` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `operation` | `sin` \| `cos` \| `sincos` \| `atan2` | yes | Operation and port-set selector. |
| `implementation` | `auto` \| `software` \| `hardware` | no | Requested backend; defaults to `auto`. |
| `iterations` | integer 1–32 | no | Requested CORDIC iteration count; defaults to 16. |

All angles use radians. `atan2` follows the conventional `atan2(y, x)` order.

## `KinematicsSolve`

Solves forward or inverse kinematics for a configured robot model.

| Direction | Port | Type | Availability | Description |
| --- | --- | --- | --- |
| Input | `joint_positions_rad` | `vector<number>` | `forward` | Joint vector to transform. |
| Output | `end_effector_pose` | `pose` | `forward` | Pose computed from the joint vector. |
| Input | `target_pose` | `pose` | `inverse` | Desired end-effector pose. |
| Input | `seed_joint_positions_rad` | `vector<number>` | `inverse` | Optional initial solver seed. |
| Output | `joint_positions_rad` | `vector<number>` | `inverse` | Solved joint vector. |
| Output | `converged` | `boolean` | `inverse` | Whether the requested tolerance was met. |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `mode` | `forward` \| `inverse` | yes | Selects the applicable ports. |
| `robotModel` | string | yes | Stable robot-model identifier. |
| `maxIterations` | positive integer | no | Inverse-solver limit; defaults to 100. |
| `tolerance` | positive number | no | Inverse-solver positional tolerance in metres; defaults to `0.0001`. |

## `ControlLoop`

Runs one discrete PID control step.

| Direction | Port | Type | Description |
| --- | --- | --- |
| Input | `setpoint` | `number` | Desired process value. |
| Input | `measurement` | `number` | Measured process value. |
| Input | `enable` | `boolean` | Optional run enable; an unconnected input defaults to `true`. |
| Output | `command` | `number` | Clamped controller output. |
| Output | `error` | `number` | `setpoint - measurement`. |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `kp`, `ki`, `kd` | number | yes | PID gains. |
| `samplePeriodUs` | positive integer | yes | Controller period in microseconds. |
| `outputMin`, `outputMax` | number | yes | Inclusive output limits; `outputMin` must be no greater than `outputMax`. |
| `initialOutput` | number | no | Initial controller output; defaults to `0`. |

## `ActuatorOutput`

Writes a command to a named actuator endpoint.

| Direction | Port | Type | Description |
| --- | --- | --- |
| Input | `command` | `number` | Requested actuator command. |
| Input | `enable` | `boolean` | Optional output enable; an unconnected input defaults to `true`. |
| Output | `applied` | `number` | Command after output limits and enable handling. |
| Output | `active` | `boolean` | Whether a command was emitted. |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `target` | string | yes | Stable actuator identifier, such as `joint1.motor`. |
| `unit` | string | yes | Command unit, such as `rad/s` or `percent`. |
| `min` | number | yes | Inclusive lower command bound. |
| `max` | number | yes | Inclusive upper command bound; must be at least `min`. |
| `safeValue` | number | no | Value used while disabled; defaults to `0`. |

## `Publish`

Publishes a value to telemetry, logs, or a host-facing graph output.

| Direction | Port | Type | Description |
| --- | --- | --- |
| Input | `value` | `any` | Value to publish. |
| Input | `timestamp_us` | `number` | Optional source timestamp. |
| Output | `published` | `boolean` | Whether this execution accepted the payload. |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `topic` | string | yes | Unique topic or output name within the deployment. |
| `transport` | `telemetry` \| `log` \| `host` | yes | Destination category. |
| `retain` | boolean | no | Retain the latest payload for newly attached consumers; defaults to `false`. |

## `Time`

Logical graph source for elapsed runtime time.

| Direction | Port | Type | Description |
| --- | --- | --- |
| Output | `value` | `number` | Elapsed time in seconds for the current graph step. |

`Time` has no hardware resource or pin assignment. Codegen samples the SDK
`hyp_timestamp_us()` primitive once per graph-step invocation and shares the
result with all consumers.

## `CustomCode`

Emits a small, scoped user-supplied C body. It is intended for application
formatting at the graph boundary, not for replacing the firmware/runtime.

| Direction | Port | Type | Description |
| --- | --- | --- |
| Input | each configured name in `params.inputs` | `number` | Stable C float variable made available to the body. |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `inputs` | non-empty array of identifiers | yes | Input port names and generated C variable names. |
| `code` | string | yes | C statements emitted inside a scoped graph-step block. |

The body may use SDK declarations from `hyprccel.h`, including
`hyp_actuator_write()` for an existing UART resource. The current UART graph
node/runtime boundary does not provide a formatted-string API, so packet
serialization and any raw UART write must be expressed in this body using the
existing SDK primitive; this node does not add a transport protocol.

## Hardware I/O nodes

Hardware I/O nodes reference configured canonical resources. They do not contain
physical pin numbers; Hardware Setup resolves the resource through the generated
board configuration before the SDK runtime accesses the ESP32 peripheral.

| Node | Ports | Resource type | Parameters |
| --- | --- | --- | --- |
| `GPIOInput` | `value: boolean` output | `gpio.*` | `samplePeriodUs` optional; `invert` optional |
| `ADCInput` | `value: number` output | `adc.*` | `samplePeriodUs`, `unit`, `minValue`, `maxValue` optional |
| `PWMOutput` | `value: number`, optional `enable: boolean` inputs; `applied: number`, `active: boolean` outputs | `pwm.*` | `min`, `max` required; `unit`, `safeValue` optional |

`ADCInput` reads the SDK's numeric ADC value and applies the configured optional
output limits. `PWMOutput` clamps its input to `min..max`, maps that range to the
SDK's normalized `0..1` duty-cycle value, and uses `safeValue` when disabled.
