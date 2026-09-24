# MBD graph contract: desktop ↔ web editor

Phase 0 spec for desktop MBD parity. It is the shared contract for the desktop
canvas (A), node palette and types (B) and persistence/codegen bridge (C).

Every rule below comes from the frozen web implementation:

| Area | Source |
|---|---|
| Editor | `mbd/editor/src/graph_editor.html` |
| Server | `mbd/editor/server.js` |
| Codegen | `mbd/codegen/graph_to_c.js` |

Line numbers are as of commit `43d1a85`. If this document and the web source
disagree, the web source wins. Re-read it; never guess a "reasonable" shape.

**Off-limits:** `mbd/editor/**` and `mbd/codegen/graph_to_c.js`. Read them;
don't modify them.

Two other documents are older and describe a superset or variant of what the
web actually does. Where they differ from this contract, this contract wins:

- `mbd/schema/graph.schema.json`
- `mbd/nodes/node_spec.md` (e.g. `KinematicsSolve`, typed `SensorInput.value`)

---

## 1. On-disk graph document

### 1.1 Location and file identity

| What | Where / rule |
|---|---|
| Graph files | `<projectsRoot>/<projectId>/graphs/<graphId>.json` |
| `projectsRoot` | `$HYPRACCEL_PROJECTS_ROOT`, else `<repo>/.hypraccel/projects`. Already implemented as `Hypr::ProjectStore::defaultRoot()`. |
| `graphId` grammar | `^[A-Za-z][A-Za-z0-9_-]{0,63}$` (`GRAPH_ID`, server.js:43). Same as the project id. |
| Filename | Must equal the document's top-level `id`. The server rejects a mismatch on read (`readProjectGraph`) and on list (`listProjectGraphs`). |
| Symlinks | A graph file must not be a symlink. |
| Real path | `graphs/` must resolve inside the project dir. |
| Listing | Only `*.json` files whose stem matches `GRAPH_ID` count. They are sorted by filename (`localeCompare`). |

Every listed file must parse and pass the section 1.3 check. One bad file makes
the whole list call fail.

**Project manifest.** `project.json` (`hypraccel.project` v1) holds these
graph-related fields:

- `graphs: {"path":"graphs"}`
- `activeGraphId`
- `generatedGraphId`
- `buildGraphId`
- `updatedAt` (ISO string)

The manifest never contains graph content.

**Legacy projects.** Older projects have `graph/graph.json` and no
`graphs.path`. The server migrates them lazily (`migrateLegacyProjectGraph`,
server.js:401):

1. Copy the legacy graph to `graphs/<id>.json`, unless that file exists.
2. Set `activeGraphId` if it is unset.
3. Delete `manifest.graph`.
4. Set `graphs:{path:"graphs"}`.
5. Leave the legacy file in place.

Desktop must do the same when it opens such a project.

### 1.2 Serialization: must be byte-identical to the server

The server writes with `writeJson` (server.js:256):

```js
JSON.stringify(value, null, 2) + '\n'   // UTF-8, 2-space indent, trailing LF
```

All five existing demo graphs are byte-identical to that re-serialization
(verified). Desktop must write the same bytes, which means
`QJsonDocument::toJson()` cannot be used:

- It indents with 4 spaces.
- It sorts object keys alphabetically.

Agent C owns a small writer, `desktop/src/project/`, that reproduces
`JSON.stringify(v, null, 2)`.

**Key order is insertion order.** Keep each object's keys in the order they were
loaded or created. The canonical order for newly created objects is section 1.3.

- `QJsonObject` loses order, so the in-memory model must store ordered keys (for
  example `QList<QPair<QString,QJsonValue>>`) or keep the original key order
  alongside.
- A JS quirk: integer-like keys (`"0"`, `"12"`) come first, in ascending order.
  Not expected in practice; mirror it if cheap.

**Whitespace.**

| Case | Output |
|---|---|
| Separator after a key | `": "` |
| Empty object | `{}` |
| Empty array | `[]` |
| Line breaks | `\n` only |

**Numbers.** Use JS `Number.prototype.toString` rules:

| Value | Output |
|---|---|
| Integers with \|x\| < 1e21 | Plain digits, no `.0` (`1.0` round-trips to `1`) |
| Other values | Shortest round-trip digits, e.g. `QString::number(v,'g',QLocale::FloatingPointShortest)`, then normalized |
| Exponent form | Used only when the exponent is ≥ 21 or ≤ -7, written as `1e+21` / `1e-7` |
| `-0` | `0` |
| NaN / Infinity | `null` (JS behaviour; don't produce these) |

**Strings.** `JSON.stringify` escapes these and emits everything else raw
(UTF-8), including non-ASCII and `/`:

- `"` and `\`
- `\b \f \n \r \t`
- Other U+0000–U+001F as `\u00xx`, lowercase hex
- Lone surrogates as `\udxxx`

**Omitted values.** Keys whose value is `undefined` are dropped entirely. For
example, `metadata.targetBoard` is omitted when no board is known, so `metadata`
can be `{}`.

### 1.3 The document as the web editor saves it

Every web save goes through `toGraph()` (graph_editor.html:1467). The
server-side save then adds only the steps listed after the example. Canonical
key order:

```json
{
  "format": "hypraccel.mbd.graph",
  "version": 1,
  "id": "<graphId>",
  "name": "<display name>",
  "nodes": [
    { "id": "<nodeId>", "type": "<NodeType>", "label": "<label>",
      "params": { ... }, "position": { "x": <num>, "y": <num> } }
  ],
  "edges": [
    { "id": "<edgeId>", "from": { "node": "<nodeId>", "port": "<outPort>" },
                        "to":   { "node": "<nodeId>", "port": "<inPort>" } }
  ],
  "inputs": [ ... ],
  "metadata": { "targetBoard": "<boards.yaml key>", "externalBindings": [ ... ] }
}
```

**Derived fields.** `toGraph()` rebuilds these on every save:

- **`inputs`**: `[{"name":"angle_rad","type":"number","unit":"rad"}]` if the
  graph has any `CordicOp` node, else `[]`.
- **`metadata`**: rebuilt from scratch, keys in this order:
  1. `targetBoard`: the Hardware Setup board. Omitted if unknown.
  2. `externalBindings`: one per CordicOp node, in node order, as
     `{"input":"angle_rad","to":{"node":"<id>","port":"angle_rad"}}`. The key is
     omitted entirely when there are no CordicOps.

  All other metadata keys are dropped.
- **Other top-level keys** (`description`, `$schema`, ...) are dropped on save.

**Why desktop must derive them identically.** Codegen reads `inputs` and
`metadata.externalBindings` (graph_to_c.js:377, 385). An unconnected CordicOp
`angle_rad` generates C only through that binding.

**Node objects.**

- **Keys:** exactly `id`, `type`, `label`, `params`, `position`, in that order.
  Unknown node keys are dropped.
- **`position`:** `{x,y}`. A missing position loads as `{x:0,y:0}`.
- **`params`:** preserved verbatim, including unknown keys and their order.
  - Editing an existing key keeps its place.
  - A newly set key is appended.
  - Setting `hardwareResource` to `""` deletes the key (`updateParam`,
    graph_editor.html:1162).

**Edge objects.** Keys are `id`, `from`, `to`; endpoints are `{node, port}`. If a
loaded edge has no `id`, the web assigns
`${from.node}-${from.port}-${to.node}-${to.port}-${index}` on load and writes it
on the next save.

**Server-side save steps.**

1. **POST (create):** sets `graph.id = graphId` and
   `graph.name = trim(body.name || graph.name)`, falling back to `graphId`.
2. **PUT:** sets `graph.id`, and `graph.name` if a name is given.
3. **`writeProjectGraph`:**
   - Re-trims `name`: max 120 characters, falls back to the id.
   - Assigning an existing key keeps its position.
   - Asserts the document (below).
   - Writes the file.
   - If `manifest.generatedGraphId` or `buildGraphId` equals this graph, deletes
     `generated/` and `build/`, and clears `generatedGraphId` only.
   - Sets `activeGraphId = graphId` and `updatedAt = now` in the manifest.
   - All manifest writes use `writeJson` too.

`assertGraphDocument` (server.js:374) is the minimum validity check. The
document must be an object with:

- `format === "hypraccel.mbd.graph"` and `version === 1`
- `id` matching `GRAPH_ID`
- `nodes` and `edges` arrays

Desktop applies the same check on load and refuses anything else.

**Rename and delete.** Desktop does them the way server.js:877 and :909 do:

- **Rename:**
  1. Rewrite the file with the new `id` and `name` under the new filename.
  2. Delete the old file.
  3. Remap `activeGraphId`.
  4. Null out `generatedGraphId` / `buildGraphId` if they pointed at the old graph.
  5. Delete `generated/` and `build/` in that case.
- **Delete:**
  - Refused if it is the last graph in the project.
  - When the deleted graph was active, `activeGraphId` moves to the next listed
    graph.

These are stretch goals for C. Save, Save As and Open are required.

**On open.** The web picks the graph to open in this order:

1. The `activeGraphId` in the manifest.
2. Otherwise, the first listed graph.

After loading it sets `activeGraphId` to that graph (the `/activate` call).
Desktop should do the same.

### 1.4 IDs and labels created in the editor

| What | Rule | Source |
|---|---|---|
| New node id | `type.replace(/[^A-Za-z]/g,'').toLowerCase() + '_' + nextId++`, e.g. `cordicop_3`, `gpioinput_4` | graph_editor.html:1127 |
| `nextId` after a load | `max(numeric suffix of /_(\d+)$/ over all node ids, 0) + 1`. Starts at 1 for an empty graph. | graph_editor.html:1509 |
| New node label | `(displayNames[type] \|\| type) + ' ' + id`, e.g. `"CORDIC cordicop_3"`. On load a missing label gets the same default. | |
| New node params | Deep copy of `defaults[type]` (section 2) | |
| New node position | The drop point, projected into canvas coordinates (`rfInstance.project`, graph_editor.html:2150). If there isn't one, `{x:120+nextId*15, y:120+nextId*10}` using the already-incremented `nextId`. | |
| New edge id | `source + '-' + sourcePort + '-' + target + '-' + targetPort` | graph_editor.html:1979 |
| New graph id | From the name: `graphIdFromName`. Agent C: copy the exact rule from graph_editor.html:1046. | |

---

## 2. Node types

**Where the definitions live.** They are inline JS literals in
`graph_editor.html` (lines 885–996). There is no shared node-definition file:
`node_spec.md` and `graph.schema.json` are documentation and don't drive the
editor.

| Literal | What it defines |
|---|---|
| `defaults` | New-node params; its key order = palette/inspector order |
| `descriptions` | Palette tooltip and search text |
| `displayNames` | Currently only `CordicOp` → "CORDIC" |
| `categories` | Palette grouping and order |
| `portTypes` + `ports(type, params)` | Port lists |
| `getSelectOptions(key, nodeType)` (line 1174) | Select options |
| `renderConfig` (line 1253) | Inspector field groups (an if-chain, but regular) |
| `expectedTypeMap` inside `renderConfig` | Allowed hardware-resource types |

**Extraction is proven to work.** A prototype slices the literal block and
`getSelectOptions` out of the HTML, evaluates them with `new Function`, and
regex-parses the `renderConfig` groups. It recovers all 24 types, and the
inspector fields match the `defaults` key order for every type. The table in
section 8 was produced that way. B commits the result as JSON (section 6, D1).

### 2.1 Dynamic port rules

Port lists come from `ports()`. Everything else is static `portTypes[type]`.

| Node | Condition | Inputs | Outputs |
|---|---|---|---|
| `CordicOp` | `operation` is `sin` or `cos` | `angle_rad` | `value` |
| `CordicOp` | `operation === "sincos"` | `angle_rad` | `sin`, `cos` |
| `CordicOp` | `operation === "atan2"` | `y`, `x` | `angle_rad` |
| `CustomCode` | always | one `number` input per `params.inputs` entry | none |

The port type strings are display labels only (section 3). Two of them are
irregular: `any`, and `"number (optional)"` on `Publish.timestamp_us`.

### 2.2 Inspector behaviour to replicate

**Field kinds.**

| Kind | Control | Behaviour |
|---|---|---|
| `text` | Line edit | Stores the string on every keystroke. |
| `number` | Text entry | Stores `Number(raw)` when finite. Ignores the partials `''`, `'-'`, `'.'`, `'-.'`, `'….'`. On blur, invalid or empty input becomes `0`. |
| `check` | Checkbox | Stores a boolean. Label is "Enable retained message" for `retain`, "Enabled" otherwise. |
| `select` | Dropdown | Options from `getSelectOptions`. Stores the option value. |

**Web quirk:** the select `onChange` stores `e.target.value`, which is always a
**string**. So `dataBits`/`stopBits` picked in the web become `"7"`, not `7`.
Codegen ignores these params, so the C output is unaffected (see D4).

**Special groups.**

- **`CustomCode`** has a "Custom C Body" group:
  - `inputs`: a single line edit. Save = split on `,`, trim, drop empties.
    Display = join with `", "`.
  - `code`: a textarea.
- **Hardware nodes** (`SensorInput`, `ActuatorOutput`, `GPIOInput`, `ADCInput`,
  `PWMOutput`, `UARTInput`, `UARTOutput`, `EncoderInput`, `MotorOutput`) have a
  "Hardware Setup" group with a `hardwareResource` dropdown:
  - Source: the project `hardware.json` `resources`.
  - Shown: resources with `available == true` whose `type` is in
    `expectedTypeMap[type]`.
  - Label: the resource id, plus `" (unassigned pin)"` if no
    `hardware.assignments[].resource` matches it.
  - The first entry is empty: "— no hardware resource —".
  - Below the dropdown: a pin summary `ROLE → pin` built from the resource's
    `assignments`.
  - Desktop gets this from `ProjectStore`. `hardwareJson()` produces the same
    shape.
- **`CordicOp`** has an "Accelerator / CORDIC" group with a `hardwareResource`
  dropdown:
  - Lists resources with `type === "accelerator"`, `available`, and a non-empty
    `configuration`.
  - The empty entry is "— no accelerator resource —".

---

## 3. Connection and validation rules

### 3.1 What the web editor enforces client-side

Nothing type-related.

- `onConnect` (graph_editor.html:1978) accepts any connection React Flow
  produces:
  - Only source handle (output) → target handle (input).
  - Self-loops aren't blocked.
  - Duplicate edges aren't blocked.
  - Several edges into one input aren't blocked.
- There is no `isValidConnection`.
- There are no required-parameter checks. The number fields just coerce bad
  input to 0.
- Deleting a node also removes its edges.
- `Delete` / `Backspace` deletes the selected edge or node immediately. Only
  the right-click "Delete Node" path asks for confirmation.

Existing graphs rely on this looseness:

- `mbd/test_graphs/pid_test.json` wires `Constant.value` (number) into
  `ControlLoop.enable` (boolean).
- Four graphs wire `number` into `Publish.value` (any).

Codegen accepts all of these.

### 3.2 What `graph_to_c.js` enforces (the real validity gate)

**Graph and node checks.**

- **Document:** `format` and `version` as above; `nodes` and `edges` must be
  arrays.
- **Node ids:**
  - Unique.
  - Match `^[A-Za-z][A-Za-z0-9_-]*$`.
  - Still valid as C identifiers after `-`→`_`, with no C keyword and no
    collisions.
- **Every node needs a `params` object.**
- **Supported types:** exactly the 24 in section 8. `KinematicsSolve` and
  anything else is rejected.
- **`hardwareResource`** must match
  `^(gpio|uart|spi|i2c|pwm|adc|accelerator|encoder|motor)\.[A-Za-z0-9_-]+$` with
  the right prefix. It is required for:

  | Node | Required prefix |
  |---|---|
  | `GPIOInput` | `gpio` |
  | `ADCInput` | `adc` |
  | `PWMOutput` | `pwm` |
  | `UARTInput`, `UARTOutput` | `uart` |
  | `EncoderInput` | `encoder` |
  | `MotorOutput` | `motor` |
  | `SensorInput` | `gpio`, `adc` or `uart` |
  | `ActuatorOutput` | `gpio`, `pwm` or `uart` |

- **`CustomCode`:**
  - `inputs` is a non-empty array of unique identifiers, none equal to the node
    symbol.
  - `code` is a string.

**Edge checks.**

- Both endpoint nodes must exist.
- The `from.port` must be a *generated output*. That is narrower than the
  editor's port list:
  - Editor-visible outputs that codegen rejects as edge sources:
    - `ActuatorOutput.applied` / `.active`
    - `Publish.published`
  - `CordicOp` `atan2` is rejected as an operation ("not supported by the SDK")
    even though the palette offers it.
- The `to.port` must be in the node's input list.
- At most one edge per input port.
- No cycles (topological sort).

**Required input edges.**

| Node | Required inputs | Optional |
|---|---|---|
| `CordicOp` | `angle_rad` (edge or external binding) | |
| `PWMOutput` | `value` | |
| `ActuatorOutput` | `command` | |
| `Publish` | `value` | |
| `Add`, `Subtract`, `Multiply` | `a`, `b` | |
| `Gain`, `Compare`, `Saturation` | `input` | |
| `Switch` | `condition`, `true_value`, `false_value` | |
| `ControlLoop` | `setpoint`, `measurement` | `enable` |
| `UARTOutput` | `data` | |
| `MotorOutput` | `command` | |
| `WheelSpeed` | `encoder_count` | |
| `DifferentialDrive` | `linear_velocity`, `angular_velocity` | |
| `CustomCode` | every entry in `inputs` | |

**Numeric parameter checks.** Values must be finite numbers where used:

| Node | Rule |
|---|---|
| `PWMOutput`, `MotorOutput` | `min < max` |
| `ADCInput` | `maxValue ≥ minValue` |
| `SensorInput` | `valueType` must be `"number"` |

**Graph inputs.** Must be of type `number`. `externalBindings` may only target
`CordicOp.angle_rad`.

### 3.3 What the server adds before codegen (server-only, frozen)

`validateGraphHardwareResources` (server.js:669) runs in `/api/build` and in
materialize:

- **Target board:** `metadata.targetBoard`, if set, must equal the hardware
  board. Otherwise the error is "stale target".
- **Resource checks** apply to `SensorInput`, `ActuatorOutput`, `GPIOInput`,
  `ADCInput`, `PWMOutput` and `CordicOp` nodes that have a `hardwareResource`:
  - The resource id must have the right prefix; `CordicOp` needs `accelerator.`.
  - It must exist in `hardware.resources`.
  - It must be configured: assigned, or accelerator, or has a non-empty
    `configuration`.

This lives in frozen server.js, not in a shared module. See D3.

---

## 4. Codegen invocation (what "Generate" must do)

**Invocation today.** `/api/build` (server.js:1072) and `materializeEsp32`
(server.js:1181) do this:

```
node <repo>/mbd/codegen/graph_to_c.js <tmp>/graph.json <tmp>/graph.c
```

- `node` is `process.execPath`.
- `<tmp>` is a fresh `mkdtemp` dir, deleted afterwards.
- `graph.json` is written with `JSON.stringify(graph, null, 2)`, no trailing
  newline. For a project the `graph` object is read from disk.
- The call is made with `execFileSync`. Its cwd is the server's cwd; the script
  doesn't use cwd.

**Filename matters for byte parity.** The generated header embeds
`path.basename(graphPath)`:

```
 * Generated by HyprAccel MBD-T3 graph_to_c.js from graph.json.
```

Desktop must name its temp input exactly `graph.json`, or the C differs in line
2. JSON whitespace in the temp file does not matter; only the parsed value does.

**Results.**

- **Success:** exit code 0. stdout is `Generated <outputPath>\n`. The C file is
  UTF-8, with `\n` line endings and a trailing `\n`.
- **Failure:** exit code 1. stderr is `graph_to_c: <message>\n`. The server
  returns `err.stderr.trim()` to the UI.
- **Usage error:** exit code 1 with a usage line.

**What the web "Generate" button actually runs.** It is
`POST /api/projects/:id/graphs/:graphId/generate`, which calls
`materializeEsp32`:

1. Save the graph.
2. Run pin conflicts through the shared `check_pin_conflicts`. Errors block the
   run; warnings pass through.
3. Run `validateGraphHardwareResources`.
4. Run `graph_to_c`.
5. Wipe and rewrite `generated/` with:
   - `graph.c`
   - the SDK sources
   - `hyp_board_config.h`: `gen_board_config.js` plus `injectProjectDefines`
   - a generated `main.cpp`
6. Write `platformio.ini`.
7. Set `manifest.generatedGraphId`.

The graph → C step alone is `/api/build`. The phrase "shell out to graph_to_c.js
exactly as the web server does" maps to the `/api/build` step. Full
materialization is a separate question (D2).

**How desktop invokes Node today.** It doesn't shell out to
`gen_board_config.js --project` (the brief assumed it does). Its only Node
invocation is `Hypr::PinCheckRunner` (`desktop/src/pincheck/`), which runs
`node boards/codegen/check_pin_conflicts.js`:

- The Node binary comes from `findNodeExecutable()`: `$HYPRACCEL_NODE`, else
  `node` on PATH.
- The call is async through `QProcess`, with an "unavailable" state when Node is
  missing.

The codegen bridge (C) should reuse `findNodeExecutable()` and the same QProcess
pattern:

- Pass the script path as a compile definition, like `BOARD_CATALOG_PATH`.
- Write `graph.json` into a `QTemporaryDir`.
- Show stdout/stderr and the exit status in the UI.

**Parity test oracle.** For any project graph file `G`, desktop Generate output
must equal:

```
node mbd/codegen/graph_to_c.js <dir>/graph.json <out>
```

where `<dir>/graph.json` is a copy of `G`. It must also equal the `source`
returned by `POST /api/build?projectId=…&graphId=…`, whenever D3's server-only
checks pass.

---

## 5. Required behaviours per agent (summary)

### A. Canvas and model

The model is the section 1.3 document, not a Qt-native reinterpretation.

- **Source of truth:** one model object that the canvas, inspector, palette and
  persistence all read and write. Same pattern as `PinAssignmentModel`.
- **Model contents:**
  - Ordered nodes with `id`, `type`, `label`, ordered `params`, and `position`.
  - Ordered edges with `id`, `from`, `to`.
  - Graph `id` and `name`.
  - `nextId` per section 1.4.
- **Canvas:** generic node box with the type title (or display name) and label,
  input ports on the left, output ports on the right. Port lists come from B's
  type registry through an interface. A doesn't hardcode them.
- **Edges:**
  - Output → input only.
  - Deletable.
  - The edge id rule from section 1.4.
- **Other interactions:** drag, pan and zoom, plus node and edge selection.
  Delete removes a node together with its edges.
- **Undo/redo:** the web has it (`remember`/`undo`/`redo`). Nice-to-have for A.

### B. Palette and types

- **Registry:** data-driven from the web source (D1). Provides, per type:
  - ports as a function of params (section 2.1)
  - defaults
  - category
  - description
  - display name
  - inspector groups
  - select options
  - hardware-resource types
- **Palette:** categories in web order, with a search that matches type name and
  description. Drag onto the canvas creates a node per section 1.4.
- **Inspector:** replicate section 2.2.
- **Connection rules:** per D5.

### C. Persistence and codegen

- **Writer:** byte-exact per section 1.2.
- **Save/load:** per section 1.3, including:
  - the derived `inputs` / `metadata`
  - manifest updates
  - artifact invalidation
  - legacy migration
- **Generate:** per section 4.
- **Round-trip tests:**
  - **Load and save:** every existing graph listed below must load and save back
    byte-identically.
    - `.hypraccel/projects/demo_project/graphs/*`
    - the `mbd/editor/test` fixtures
  - **Desktop-saved graph:** it must equal what `toGraph()` + `writeJson` would
    produce for the same editor state.
  - **Codegen parity:** desktop Generate on each graph must equal the section 4
    oracle.
- **Test graphs that don't round-trip byte-identically:**
  `mbd/schema/examples/*` and `mbd/test_graphs/pid_test.json`. They aren't
  `toGraph`-shaped (extra `$schema`/`description`, different key order).
  - **Loading** must still work.
  - **Saving** normalizes them exactly as the web would.

---

## 6. Decisions (approved 2026-09-24)

- **D1: node definitions come from a committed JSON, checked by a drift test.**
  - There is no build-time extraction. The desktop C++ build must not depend on
    Node or on how the HTML is structured.
  - Agent B commits `desktop/src/mbd/nodes/node_types.json` and a regeneration
    script, `desktop/src/mbd/nodes/extract_node_types.js`:
    - It reads `graph_editor.html` and `graph_to_c.js`.
    - It writes the JSON.
    - With `--check`, it exits non-zero on any difference.
  - The drift test runs `extract_node_types.js --check` as a ctest and skips when
    Node is missing (the same pattern `pin_check_runner_test` uses). It fails as
    soon as the web adds, removes or changes a node type, port, default,
    category, inspector field, select option or hardware-resource map. It also
    fails if codegen's set of generated source outputs changes.
  - The app loads the JSON as a Qt resource.
- **D2: Generate matches `/api/build` only.**
  1. Save the graph.
  2. Run `graph_to_c.js` per section 4.
  3. Show the generated C or the stderr in the UI.
  4. Write nothing into `generated/`, and leave the manifest `generatedGraphId`
     alone.

  Full materialization (SDK copy, board header, `main.cpp`, `platformio.ini`,
  manifest) is a separate follow-up.
- **D3: skip the server's `validateGraphHardwareResources` check.** Section 3.3
  describes the check. The gap must be visible in two places:
  1. **Generate panel:** one line that says desktop Generate doesn't yet check
     for a stale target board or unconfigured hardware resources, which the web
     server refuses before codegen.
  2. **Docs:** the same sentence in `desktop/README.md`.
- **D4: desktop stores select values with their JSON type.** `dataBits` and
  `stopBits` stay numbers. Agent C must **verify** that the web editor then
  shows them correctly, not just that the C matches:
  - A test must show that a desktop-saved graph with `dataBits: 7` (number)
    renders "7" selected in the web inspector.
  - It can check the web's select-rendering logic directly: React 17's
    controlled `<select value>` matches options by `'' + value`. Or it can load
    the page headlessly.
  - It must also show that a web-saved `"7"` (string) loads and displays
    correctly in desktop.
  - If the web would show a desktop-saved number wrongly, stop and report. Don't
    work around it by switching to strings silently.
- **D5: connections are permissive, like the web, with codegen's certain
  rejections blocked.**
  - **Allowed:** any output → input.
  - **Blocked, with a reason:**
    - a second edge into an already-connected input
    - a self-loop
    - an edge that would create a cycle
    - a source port that codegen can't use as a generated output (section 3.2;
      from `node_types.json` `codegenSources`)
    - output → output or input → input
  - **Warning (allowed):** port type labels differ and the target isn't `any`.
    Treat `number (optional)` as `number`.
  - **Loading:** a loaded graph that already violates these rules still loads
    unchanged.

## 7. Module ownership and interfaces (Phase 1)

**Shared contract headers.** These are integrator-owned, in
`desktop/src/mbd/contract/`. Include them; do not edit them. Propose any change
in your report.

- **`ordered_json.h`:** an insertion-ordered `Hypr::Json::Value` / `Object`
  with `find`/`set`/`remove`. It declares `parse()` and `stringify()`; Agent C
  implements them in `desktop/src/project/ordered_json.cpp`.
- **`graph_document.h`:** `GraphNode`, `GraphEdge`, `GraphDocument`, and
  `edgeIdFor()`.
- **`node_type_provider.h`:** `PortSpec`, `ConnectionCheck`, and the abstract
  `NodeTypeProvider`. The canvas depends on this interface; Agent B implements
  it.

**Top-level `desktop/CMakeLists.txt`** is integrator-owned. It already lists the
components `mbd` and `mbd/nodes` and links `studio_mbd` and `studio_mbd_nodes`
into the app. Don't edit it, `desktop/src/main.cpp`, or `hardware_view.*`. The
MBD tab wiring is Phase 2.

| Agent | Owns | Library |
|---|---|---|
| A | `desktop/src/mbd/*` except `contract/` and `nodes/` | `studio_mbd`: `GraphModel` (QObject holding a `GraphDocument` plus selection, `nextId`, undo), `GraphCanvas` (`QGraphicsView`/`QGraphicsScene`), tests |
| B | `desktop/src/mbd/nodes/*` | `studio_mbd_nodes`: `NodeTypeRegistry : NodeTypeProvider` (loads `node_types.json` from a Qt resource), `NodePalette` widget, `NodeInspector` widget, `node_types.json`, `extract_node_types.js`, tests |
| C | `desktop/src/project/*`: add files, and edit `project/CMakeLists.txt` | Additions to `studio_project`: `ordered_json.cpp`, `GraphStore` (list/load/save/rename/delete, `toGraph` derivation, legacy migration, manifest updates), `GraphCodegenRunner` (QProcess), `CodegenOutputView` widget, tests |

**Cross-agent seams, fixed now so the agents can build in parallel.**

- **Palette to canvas drag.** Mime type `application/x-hypraccel-node`; the
  payload is the UTF-8 type key.
  - The canvas handles the drop with
    `model.addNode(type, provider.defaultParams(type), scenePos, provider.displayName(type))`.
  - `addNode` applies the section 1.4 id and label rules.
- **Inspector editing.** `NodeInspector` edits params only through
  `GraphModel`. Agent A must provide at least:
  - `const GraphDocument &document() const`
  - `const GraphNode *node(const QString &id) const`
  - `QString selectedNodeId() const`
  - `void setParam(const QString &nodeId, const QString &key, const Json::Value &)`
  - `void removeParam(const QString &nodeId, const QString &key)`
  - signals `selectionChanged()`, `nodeChanged(const QString &nodeId)` and
    `documentReset()`

  To avoid a build-time dependency on A's library, B builds its inspector
  against a small abstract adapter declared in B's own header,
  `NodeInspector::Source`. Phase 2 bridges it to `GraphModel`.
- **Hardware resources.** The inspector takes a `QJsonObject hardware` (the
  project `hardware.json`; `ProjectStore::hardwareJson()` shape) through
  `setHardware()`. It never reads files itself.
- **Persistence.** `GraphStore` works on `GraphDocument` and knows nothing about
  `GraphModel`. The target board comes in as a parameter (`QString`, empty means
  omit `targetBoard`).
- **Registry file format.** `node_types.json` is read with `QJsonDocument`.
  Anything whose order matters (defaults, groups, fields, categories, select
  options) is stored as arrays. Example:
  `"defaults": [{"key":"operation","value":"sin"}, ...]`.
  B must never rely on `QJsonObject` key order.

**Builds and tests.**

- Each agent uses its own build dir:
  `nix-shell desktop/shell.nix --run 'cmake -S desktop -B desktop/build-mbd-<a|b|c> -G Ninja && cmake --build desktop/build-mbd-<a|b|c> && ctest --test-dir desktop/build-mbd-<a|b|c> --output-on-failure'`
- Another task is editing `desktop/src/board/*`, `desktop/src/config/tests/*`,
  `boards/*` and `.hypraccel/hardware.json` right now. Don't touch those files.
  If they break your build, report it rather than fixing it.
- Don't commit.

---

## 8. Node table (extracted mechanically from graph_editor.html @ 43d1a85)

| Type | Palette category | Inputs (name:type) | Outputs | Default params (web `defaults`) | Inspector groups: fields(kind) | hardwareResource types |
|---|---|---|---|---|---|---|
| `SensorInput` | Inputs & Sensors | — | `value`:any, `timestamp_us`:number, `valid`:boolean | `{"source":"imu.yaw_rad","valueType":"number","samplePeriodUs":1000,"unit":"rad"}` | Sensor Settings: source(text), valueType(select), samplePeriodUs(number), unit(text) | gpio, adc, uart |
| `CordicOp` (shown as "CORDIC") | Accelerator | `angle_rad`:number | `value`:number | `{"operation":"sin","implementation":"auto","iterations":16}` | CORDIC Implementation: operation(select), implementation(select), iterations(number) | accelerator (own "Accelerator / CORDIC" group) |
| `ControlLoop` | Control | `setpoint`:number, `measurement`:number, `enable`:boolean | `command`:number, `error`:number | `{"kp":1,"ki":0,"kd":0,"samplePeriodUs":1000,"outputMin":-1,"outputMax":1,"initialOutput":0}` | PID Gains: kp(number), ki(number), kd(number)<br>Execution Limits: samplePeriodUs(number), outputMin(number), outputMax(number), initialOutput(number) | — |
| `ActuatorOutput` | Outputs & Actuators | `command`:number, `enable`:boolean | `applied`:number, `active`:boolean | `{"target":"joint1.motor","unit":"rad/s","min":-1,"max":1,"safeValue":0}` | Actuator Settings: target(text), unit(text), min(number), max(number), safeValue(number) | gpio, pwm, uart |
| `Publish` | Data / Telemetry | `value`:any, `timestamp_us`:number (optional) | `published`:boolean | `{"topic":"telemetry/value","transport":"telemetry","retain":false}` | Telemetry Binding: topic(text), transport(select), retain(check) | — |
| `Constant` | Signal Processing | — | `value`:number | `{"value":1}` | Value: value(number) | — |
| `Add` | Signal Processing | `a`:number, `b`:number | `value`:number | `{}` | — | — |
| `Subtract` | Signal Processing | `a`:number, `b`:number | `value`:number | `{}` | — | — |
| `Multiply` | Signal Processing | `a`:number, `b`:number | `value`:number | `{}` | — | — |
| `Gain` | Signal Processing | `input`:number | `value`:number | `{"gain":1}` | Gain: gain(number) | — |
| `Compare` | Signal Processing | `input`:number | `result`:boolean | `{"operation":"gt","threshold":0}` | Comparison: operation(select), threshold(number) | — |
| `Saturation` | Signal Processing | `input`:number | `value`:number | `{"min":-1,"max":1}` | Limits: min(number), max(number) | — |
| `Switch` | Signal Processing | `condition`:boolean, `true_value`:number, `false_value`:number | `value`:number | `{}` | — | — |
| `Time` | Timing | — | `value`:number | `{}` | — | — |
| `CustomCode` | Custom | `value`:number | — | `{"inputs":["value"],"code":"/* Inputs are available by name, for example: printf(\"%d\\n\", (int)value); */"}` | Custom C Body: inputs (comma list), code (textarea) | — |
| `GPIOInput` | Inputs & Sensors | — | `value`:boolean | `{"samplePeriodUs":1000,"invert":false}` | GPIO Settings: samplePeriodUs(number), invert(check) | gpio |
| `ADCInput` | Inputs & Sensors | — | `value`:number | `{"samplePeriodUs":1000,"unit":"V","minValue":0,"maxValue":3.3}` | ADC Settings: samplePeriodUs(number), unit(text), minValue(number), maxValue(number) | adc |
| `PWMOutput` | Outputs & Actuators | `value`:number, `enable`:boolean | `applied`:number, `active`:boolean | `{"min":0,"max":255,"unit":"%","safeValue":0}` | PWM Settings: min(number), max(number), unit(text), safeValue(number) | pwm |
| `UARTInput` | Inputs & Sensors | — | `value`:number, `valid`:boolean | `{"samplePeriodUs":1000,"baudRate":115200,"dataBits":8,"parity":"none","stopBits":1}` | UART Settings: samplePeriodUs(number), baudRate(number), dataBits(select), parity(select), stopBits(select) | uart |
| `UARTOutput` | Outputs & Actuators | `data`:number | `sent`:boolean | `{"baudRate":115200,"dataBits":8,"parity":"none","stopBits":1}` | UART Settings: baudRate(number), dataBits(select), parity(select), stopBits(select) | uart |
| `EncoderInput` | Inputs & Sensors | — | `position`:number, `velocity`:number, `valid`:boolean | `{"samplePeriodUs":1000,"pulsesPerRevolution":1024,"quadrature":true}` | Encoder Settings: samplePeriodUs(number), pulsesPerRevolution(number), quadrature(check) | encoder |
| `MotorOutput` | Outputs & Actuators | `command`:number, `enable`:boolean | `applied`:number, `active`:boolean | `{"min":-255,"max":255,"unit":"%","safeValue":0}` | Motor Settings: min(number), max(number), unit(text), safeValue(number) | motor |
| `WheelSpeed` | Robotics | `encoder_count`:number | `speed`:number | `{"pulsesPerRevolution":1024,"wheelRadiusMeters":0.05}` | Wheel Settings: pulsesPerRevolution(number), wheelRadiusMeters(number) | — |
| `DifferentialDrive` | Robotics | `linear_velocity`:number, `angular_velocity`:number | `left_cmd`:number, `right_cmd`:number | `{"trackWidthMeters":0.3}` | Drive Settings: trackWidthMeters(number) | — |

Select options (web `getSelectOptions(key,nodeType)`; `[value,label]` pairs or bare values):

- `operation` (CordicOp): `["sin","cos","sincos","atan2"]`
- `operation` (Compare): `[["gt","> (greater than)"],["lt","< (less than)"],["ge",">= (greater or equal)"],["le","<= (less or equal)"],["eq","== (equal)"],["ne","!= (not equal)"]]`
- `implementation`: `[["auto","Automatic"],["software","Software"],["hardware","Hardware Accelerator"]]`
- `transport`: `["telemetry","log","host"]`
- `valueType`: `["number","boolean","vector<number>","pose","any"]`
- `dataBits`: `[5,6,7,8]`
- `parity`: `["none","even","odd"]`
- `stopBits`: `[1,2]`

Palette categories in order (empty ones render a "Future expansion" placeholder):

- Inputs & Sensors: SensorInput, GPIOInput, ADCInput, UARTInput, EncoderInput
- Outputs & Actuators: ActuatorOutput, PWMOutput, UARTOutput, MotorOutput
- Accelerator: CordicOp
- Control: ControlLoop
- Robotics: WheelSpeed, DifferentialDrive
- Data / Telemetry: Publish
- Signal Processing: Constant, Add, Subtract, Multiply, Gain, Compare, Saturation, Switch
- Communication: _(Future expansion)_
- Timing: Time
- Hardware: _(Future expansion)_
- Custom: CustomCode

Descriptions (palette tooltip / search text):

- `SensorInput`: Board signal / sensor binding
- `CordicOp`: CORDIC accelerator with automatic, software, or hardware routing
- `ControlLoop`: Discrete PID controller algorithm
- `ActuatorOutput`: Board motor/actuator output endpoint
- `Publish`: Telemetry payload publisher
- `Constant`: Constant numeric value
- `Add`: Add two inputs (a + b)
- `Subtract`: Subtract two inputs (a - b)
- `Multiply`: Multiply two inputs (a * b)
- `Gain`: Multiply input by gain factor
- `Compare`: Compare input against threshold
- `Saturation`: Clamp input between min/max
- `Switch`: Select between true/false values based on condition
- `Time`: Elapsed runtime time in seconds
- `CustomCode`: Scoped user-defined C body with configurable numeric inputs
- `GPIOInput`: Digital GPIO input with optional inversion
- `ADCInput`: Analog-to-digital converter input
- `PWMOutput`: Pulse-width modulation output
- `UARTInput`: UART serial input (reads available bytes)
- `UARTOutput`: UART serial output (writes bytes)
- `EncoderInput`: Quadrature encoder position/velocity input
- `MotorOutput`: DC motor speed/torque output (via PWM)
- `WheelSpeed`: Converts encoder pulses to linear speed
- `DifferentialDrive`: Converts linear/angular velocity to left/right motor commands
