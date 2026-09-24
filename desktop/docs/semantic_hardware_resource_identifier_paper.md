# A Board-Descriptor-Derived Semantic Resource Identifier Convention for Cross-Layer Embedded System Configuration

## Abstract

Maintaining a consistent notion of hardware-resource identity across a hardware descriptor, code generation, graphical configuration, model-based-design (MBD) bindings, and runtime firmware is a recurring problem in embedded toolchains, where each layer has traditionally been free to invent its own names, indices, or handles. This paper audits HyprAccel, a board-descriptor-driven toolchain, against the specific claim that it implements a *semantic hardware-tag-first architecture*: one canonical tag per hardware resource, defined once at the descriptor layer (`boards/boards.yaml`) and reused unchanged by every downstream consumer. Tracing one concrete resource (`uart.uart0`, role `tx`, physical pin `GPIO1`) through seven implementation layers shows the strong claim does not hold: two related but incompatible identifier grains coexist — a two-part resource identifier `R = (type, instance)` and a three-part role-qualified identifier `F = (type, instance, role)` — and the runtime firmware's own parser rejects the three-part form outright. What does hold, and is verified with file-level evidence, is a narrower invariant: the two-part identifier is independently reconstructed from the same descriptor by every layer, cross-validated at build time between the MBD graph and the physical pin map, and resolved at runtime by the SDK against the same board descriptor. This is compared against STM32CubeMX's `GPIO_Label` mechanism, which is tag-equivalent but scoped to GPIO only, and against Simulink/Embedded Coder, which has no equivalent hardware-resource-identity concept at all. The contribution is characterized precisely as a cross-layer resource-identifier *convention* with build/compile/runtime validation, not a fully canonical tag architecture, and concrete gaps are reported as future work.

**Keywords** — hardware abstraction, model-based design, code generation, embedded toolchains, resource identifier, pin configuration, design-time verification, semantic tagging

---

## I. Introduction

Embedded system toolchains that span multiple abstraction layers — a hardware descriptor, a code generator, a graphical configuration tool, a model-based design environment, and target firmware — face a recurring integration problem: each layer typically needs its own representation of *which physical hardware resource* a given piece of configuration or generated code refers to. A pin-configuration GUI may refer to a resource by a user-assigned label; a code generator may refer to it by a `#define`; a hardware abstraction layer (HAL) may refer to it by a numbered peripheral handle; a model-based design tool may refer to it by a block or signal name. When these representations are not kept in correspondence by construction, they must be kept in correspondence by convention and by the discipline of the people maintaining the toolchain — a well-documented source of configuration drift and silent mismatches in generated-code pipelines.

Traditionally, this problem is addressed with a low-level, per-layer identifier: a numbered peripheral instance (`USART1`, `SPI2`), a positional pin index, or an opaque handle allocated at code-generation time. These identifiers are unambiguous within a single layer but carry no semantic information across layers, and nothing prevents two layers from disagreeing about what a given number or handle currently refers to.

HyprAccel's design intent, as stated in its own internal documentation and reflected in its codebase, is to instead give every hardware resource a human-readable, semantically typed identifier — e.g., `uart.uart0` rather than `USART1` — defined once from the board descriptor and reused by every downstream layer: code generation, build-time conflict checking, the desktop pin/clock configuration UI, the MBD graph editor's hardware bindings, and the SDK's runtime resource resolution.

This paper does not assume that intent was fully realized. It poses the architectural question directly:

> Can a board-descriptor-derived semantic hardware-resource identifier provide a common identity across hardware description, code generation, visual configuration, MBD graph binding, conflict validation, and runtime hardware resolution?

and answers it by tracing one identifier through the full implementation, cross-checked against the actual source at the file-and-line level, rather than assuming the architecture description is accurate. The audit underlying this paper was deliberately structured to look for the places the claim breaks, not only the places it holds, on the premise that a paper whose central claim is disproved by the first counterexample a reviewer finds is worse than one that states its own limitations. Broad claims about embedded toolchains in general are avoided except where directly supported by the STM32CubeMX and Simulink/Embedded Coder findings in Section V.

---

## II. Architectural Model

Three distinct concepts recur throughout the trace and are frequently conflated in informal descriptions of the system. This section defines them precisely before the trace uses them.

### A. Resource identifier, *R*

$$R = (\text{type}, \text{instance})$$

A two-part identifier naming a logical hardware resource independent of any signal role, rendered as a dotted string, e.g. `uart.uart0`, `spi.hspi`, `i2c.wire`. This is the identifier grain used for build-time conflict checking (Section III, Hop 2) and for the SDK's runtime resource table (Hop 4b). Empirically, it is the strongest cross-layer identifier present in the codebase: it is the only one directly compared as a literal string between the hardware descriptor's reconstruction and the MBD graph's stored value (Section IV-A).

### B. Resource-role (function) identifier, *F*

$$F = (\text{type}, \text{instance}, \text{role}) = R \oplus \text{role}$$

A three-part identifier that additionally names a specific signal role of a resource, e.g. `uart.uart0.tx`, `uart.uart0.rx`. This is the identifier grain used for pin-level and function-level representation: the desktop pin picker's function vocabulary and the per-project header generator's pin-function macros (Section III, Hops 3 and 5). *F* is not a refinement of *R* that every layer accepts — Section IV-B shows a layer (the SDK's runtime parser) that structurally rejects any string of this shape.

### C. Physical pin identity

A separate concept from both *R* and *F*: a string naming an actual MCU pin (`GPIO1`, `GPIO3`), independent of role or resource type. The mapping `R \times \text{role} \to \text{pin}$ (or, for resources with `pin_options`, `R \times \text{role} \to \{\text{pin}_1, \text{pin}_2, \dots\}$) is exactly the data stored under a resource's entry in the hardware descriptor.

The relationship among the three is: a resource *R* has one or more roles; each `(R, role)` pair resolves to a physical pin (or, for a minority of resources, a small candidate set of physical pins); the role-qualified pair `(R, role)` is sometimes, but not consistently, serialized as the identifier *F*. Treating *R*, *F*, and the physical pin as interchangeable — as an informal architecture description might — obscures exactly the place where the implementation's cross-layer agreement breaks down, which is between *R* and *F*, not between either of them and the physical pin.

---

## III. End-to-End Implementation Trace

This section traces one concrete resource, `uart.uart0` (`R`), role `tx` (`F = uart.uart0.tx`), physical pin `GPIO1`, through seven implementation stages in data-flow order. At each stage the representation is classified as one of: **unchanged string**, **reconstructed string**, **transformed identifier**, **macro encoding**, **physical pin**, **separate role field**, **runtime lookup**, or **UI-only representation**.

### Stage 1 — `boards/boards.yaml` (origin)

```yaml
uart:
  uart0: { tx: "GPIO1", rx: "GPIO3" }
```
`boards.yaml:173–176`.

**Classification: does not exist as a string at all.** There is no literal `"uart.uart0"` or `"uart.uart0.tx"` token in the descriptor; both *R* and *F* are implicit in the YAML path (`type` key → `instance` key → `role` key). Every downstream consumer that needs a flat identifier string reconstructs it from this nested structure independently. This is the first and most consequential finding of the trace: the identifier is canonical by inter-module *agreement*, not by *single definition* — there is no shared constant, schema `const`, or generator function that all consumers call to produce it.

### Stage 2 — `boards/codegen/gen_board_config.js` (base header codegen)

```js
const macro = `HYP_RESOURCE_UART_${inst.toUpperCase()}`;
out += `#define ${macro}_${role.toUpperCase()}_PIN ${num}  /* ${pin} */\n`;
```
`gen_board_config.js:161–172`.

**Classification: macro encoding.** *R* and role are reconstructed from the YAML path, then irreversibly folded into a C preprocessor identifier (`.` → `_`, case-folded to upper). The physical pin (`GPIO1` → `1`) is emitted as the macro's numeric value, with the human-readable pin string surviving only as a comment. This transform is unavoidable — C identifiers cannot contain dots — but it is implemented independently here, not via a shared "tag → macro" routine reused elsewhere in the codebase (cf. Stage 3, finding 7 in Section IV-B).

### Stage 3 — `boards/codegen/pin_conflicts.js` (conflict validation)

```js
resources[`${type}.${instance}`] = { id: `${type}.${instance}`, type, instance, roles, options };
```
`pin_conflicts.js:33`.

```js
const resourceId = node && node.params && node.params.hardwareResource;
```
`pin_conflicts.js:268`.

**Classification: unchanged string (the strongest instance of tag reuse in the codebase).** *R* is reconstructed once here from `boards.yaml` into the object key/`id` field `"uart.uart0"`, then compared **by string equality** against `node.params.hardwareResource`, a value written by the MBD graph editor (Stage 6). No re-encoding occurs on either side of this comparison — this is the one hop where the descriptor-derived identifier and the graph-stored identifier are the literal same string, checked as data rather than as a naming convention. Note the grain: this is *R* (two-part), not *F*; role is carried as a sibling field, not folded into this identifier.

### Stage 4 — `boards/codegen/project_defines.js` (per-project header lines)

Three independent keying schemes coexist in this one module:

```js
pinDefines.push(`#define HYP_PIN_${macroBase}_${a.role.toUpperCase()} "${a.pin}"  /* ${a.node} → ${a.resource}.${a.role} */`);
```
`project_defines.js:74` — keyed by the graph node's own name (`macroBase`, derived from `a.node`), not by *R* or *F*.

```js
function pinFunctionFor(a) {
    const type = String(a.resource || '').split('.')[0];
    if (type === 'gpio' && a.role === 'gpio') return 'gpio';
    if (type === 'pwm' && a.role === 'output') return 'pwm';
    if (type === 'adc' && a.role === 'input') return 'adc';
    return `${a.resource}.${a.role}`;
}
```
`project_defines.js:126–132`, consumed at `:145` to build `HYP_PINFUNC_<PIN>_<FUNC>`.

**Classification: reconstructed string, then macro encoding (mixed).** `pinFunctionFor()` is the one place *F* (`${a.resource}.${a.role}`, e.g. `"uart.uart0.tx"`) is materialized as a genuine three-part string from assignment data — but it exists only transiently, inside this function, before being shredded by `sanitizeMacro()` into an uppercased, non-alphanumeric-stripped macro fragment (`HYP_PINFUNC_GPIO1_UART_UART0_TX`, keyed by the *physical pin*, not by *R* or *F*). *F* itself is never stored or passed on as a string in the generated header. Separately, `project_defines.js` had two independent implementations of this header-injection logic — one in `mbd/editor/server.js`'s `injectHardwareHeader()` (materialize path) and an inline copy in `POST /api/generate` — that the module's own comment states "had already drifted" before being unified in commit `3b4b488` ("feat(codegen): feed desktop pin/clock config into board codegen via one shared path", 2026-09-23), one day prior to the underlying audit.

### Stage 5 — SDK runtime: `sdk/src/hyp_esp32_hw.cpp`

Two mechanisms, disagreeing on identifier grain:

**5a. Compile-time macros.**
```c
#if defined(HYP_RESOURCE_PWM_GPIO32) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO32_PWM))
```
`hyp_esp32_hw.cpp:897`. **Classification: macro encoding, resolved at compile time.** No identifier string exists at runtime in this path at all; the macro shape from Stages 2 and 4 gates conditional compilation and is then gone.

**5b. Runtime resource table and parser.**
```c
{ "adc.GPIO32", HYP_RESOURCE_ADC_GPIO32_PIN },
```
`hyp_esp32_hw.cpp:~1075–1120` (`hyp_resource_pin_table[]`). **Classification: runtime lookup, using *R*.** This table, and `canonical_instance_equals(instance, "uart0")`-style comparisons elsewhere in the file, are the SDK's only string-keyed runtime identifier surface, and they use the two-part grain, agreeing with Stage 3.

Critically, the runtime parser enforces this grain as a hard boundary rather than a soft convention:
```c
const char *dot = strchr(resource_id, '.');
if (!dot || dot == resource_id || dot[1] == '\0' || strchr(dot + 1, '.') != NULL) return -1;
```
`hyp_esp32_hw.cpp:1032` (`parse_resource_id()`). Any string containing a second dot — i.e., any instance of *F* — is rejected as invalid input. This is the sharpest evidence in the trace that *R* and *F* are not two views of one canonical tag: one layer's own input validation is written to reject the other layer's output shape.

### Stage 6 — Desktop: `board_catalog.{h,cpp}`, `pin_assignment_model.cpp`

`board_catalog.cpp` parses `boards.yaml` into `Board`/`Pin`/`Resource` structs and folds `pin_options` into each `Pin::functions` list as three-part strings, e.g. `"uart.uart1.tx"` (`board_catalog.cpp:85–86, 137–147`). **Classification: reconstructed string, using *F*** — the desktop UI's native vocabulary for its pin picker and "moves from" logic is the role-qualified form.

Because Stage 3/5's vocabulary is *R* and the desktop UI's native vocabulary is *F*, `pin_assignment_model.cpp` implements explicit, named bidirectional conversion:

```cpp
ResourceRole resourceRoleFor(const QString &pin, const QString &function) {
    const auto parts = function.split('.');
    if (parts.size() == 3) return {parts[0] + "." + parts[1], parts[2]};
    return {};
}

QString functionForResourceRole(const QString &resource, const QString &role) {
    if ((type == "uart" || type == "spi" || type == "i2c") && resource.count('.') == 1 && !role.isEmpty())
        return resource + "." + role;
    return {};
}
```
`pin_assignment_model.cpp:33–52`. **Classification: transformed identifier — the clearest single artifact of the two-grain problem.** The existence of two named, tested conversion functions between *R*+role and *F* within one binary is direct evidence that "one tag" is not what is implemented; it is what two internal vocabularies are translated into and out of.

### Stage 7 — MBD graph editor: `graph_editor.html`, `pin_config.html`

```js
const expectedTypeMap = {
  GPIOInput: ['gpio'], ADCInput: ['adc'], PWMOutput: ['pwm'],
  UARTInput: ['uart'], UARTOutput: ['uart'],
  EncoderInput: ['encoder'], MotorOutput: ['motor'],
  SensorInput: ['gpio', 'adc', 'uart'], ActuatorOutput: ['gpio', 'pwm', 'uart']
};
```
`graph_editor.html:1408–1419`. **Classification: UI-only representation.** This map filters which resources are offered in a node's "Canonical Hardware Resource" dropdown by resource `type`, matching the *R* grain by convention. The selected value is written to `node.params.hardwareResource` as *R* (`graph_editor.html:1427`), which is what Stage 3 reads back — so, at the level of the stored value, this hop agrees with the hardware layer. But the type-matching enforced by `expectedTypeMap` exists only in this client-side JavaScript; it is not re-checked by `pin_conflicts.js`, nor constrained by `mbd/schema/graph.schema.json`'s `uartInputNode` definition, which types `params.hardwareResource` as an unconstrained string. `EncoderInput` and `MotorOutput` additionally name resource types (`'encoder'`, `'motor'`) that `pin_conflicts.js`'s `buildResources()` never produces (only `gpio`, `pwm`, `adc`, `spi`, `i2c`, `uart`, `accelerator`, `pin_conflicts.js:19–51`), so those two dropdowns can never populate from real project data.

**Summary of Stage 1–7 transformations:** the identifier changes representation at Stages 1 (nonexistent → reconstructed), 2 (reconstructed → macro), 4 (reconstructed *F* → macro, keyed three ways in one file), 5a (macro → compiled away), 6 (*R* ↔ *F* explicit conversion), and remains a UI-only convention at 7 unless cross-checked back at Stage 3. Table I summarizes.

**Table I. Identifier representations across HyprAccel layers**

| Layer | Representation | Example | Transformation | Role |
|---|---|---|---|---|
| `boards.yaml` (Stage 1) | nested YAML path | `uart: {uart0: {tx: "GPIO1"}}` | none (identifier does not exist as a string) | descriptor of ground truth |
| `gen_board_config.js` (Stage 2) | macro encoding | `HYP_RESOURCE_UART_UART0_TX_PIN` | reconstructed *R*+role → macro | base header codegen |
| `pin_conflicts.js` (Stage 3) | unchanged string, *R* | `"uart.uart0"` | reconstructed once, then compared verbatim | build-time conflict check |
| `project_defines.js` (Stage 4) | macro encoding (3 schemes) | `HYP_PIN_<node>_<ROLE>`, `HYP_PINFUNC_<PIN>_<FUNC>` | reconstructed *F*, then shredded into macro | per-project header |
| `hyp_esp32_hw.cpp` §5a | macro encoding | `HYP_PINFUNC_GPIO32_PWM` | consumed at compile time only | conditional compilation |
| `hyp_esp32_hw.cpp` §5b | runtime lookup, *R* | `"adc.GPIO32"` | reconstructed *R*, table-matched; rejects *F* | runtime resource resolution |
| `board_catalog.cpp` / `pin_assignment_model.cpp` (Stage 6) | reconstructed string, *F*; explicit *R*↔*F* conversion | `"uart.uart1.tx"` | bidirectional transform via named functions | desktop pin picker, "moves from" |
| `graph_editor.html` (Stage 7) | UI-only filter + unchanged string, *R* | `hardwareResource: "uart.uart0"` | type filter is UI-only; stored value matches Stage 3 | MBD hardware binding |

---

## IV. Implementation Findings

### A. Where the architecture succeeds

The strongest verified invariant in the codebase is the path

$$\texttt{boards.yaml} \to R \to \texttt{node.params.hardwareResource} \to \texttt{pin\_conflicts.js}$$

`pin_conflicts.js` reconstructs *R* once from the descriptor (`:33`) and compares it by literal string equality against the value an MBD graph node stores in `hardwareResource` (`:268`), with no re-encoding on either side. This is significant because it is a genuine build-time cross-check between two independently maintained artifacts — a hand- or tool-edited hardware descriptor and a visually authored graph — using the identifier as data, not merely as a naming convention both authors happen to follow. It is exercised by automated tests (`pin_conflicts.test.js`).

This invariant extends, at the same grain, to the SDK: `hyp_resource_pin_table[]` and the `canonical_instance_equals()` comparisons in `hyp_esp32_hw.cpp` use *R* as their runtime lookup key, and `parse_resource_id()` treats any other shape as invalid input (Section III, Stage 5b). So the two-part resource identifier is not merely agreed upon in the abstract; it is the shared key across a build-time validator, a design-time graph binding, and a runtime resolution table. This should not be characterized as "the same string everywhere" — Section IV-B documents where that breaks — but as a specific, load-bearing, and correctly-agreeing three-way relationship at the *R* grain only.

### B. Where the architecture breaks down

1. **Two identifier grains, both treated as canonical by different layers.** *R* (`type.instance`) is used by `pin_conflicts.js`, the SDK's runtime table, and the graph's `hardwareResource` field. *F* (`type.instance.role`) is used as the desktop UI's native `Pin::functions` vocabulary and as `pinFunctionFor()`'s output. `pin_assignment_model.cpp:33–52` implements dedicated conversion functions between them, and the SDK's `parse_resource_id()` (`hyp_esp32_hw.cpp:1032`) actively rejects *F*-shaped input. This is the single most damaging finding against a "one tag" reading of the architecture: it is not that layers occasionally rename an identifier, but that two structurally incompatible identifier shapes are each treated as canonical within their own layer.

2. **The identifier is a convention, not a stored value.** `boards.yaml` never contains the literal string `"uart.uart0"` or `"uart.uart0.tx"`; at least four independent sites reconstruct it from the YAML path (`pin_conflicts.js:33`, `board_catalog.cpp`'s resource parsing, `project_defines.js:126–132`, and the graph editor's server-fed resource listing). These reconstructions currently agree, but nothing in the type system — no shared constant, no schema-level generator — enforces that agreement; it is presently held together by test coverage rather than by a single source of truth for the flattened string.

3. **Historical duplication, recently unified.** `mbd/editor/server.js` maintained two independent implementations of hardware-header injection — `injectHardwareHeader()` for the materialize path and an inline copy in `POST /api/generate` — that had, per the fixing commit's own message, "already drifted." Commit `3b4b488` (2026-09-23) unified them one day before this audit; the unification is not yet battle-tested by the project's own history.

4. **The alternate-candidate-pin ("moves from") model is implemented but narrow, not inert.** `PinAssignmentModel::assign()` (`pin_assignment_model.cpp:83–105`) does enforce that an *F*-shaped assignment is exclusive to one pin, evicting a prior holder and emitting a change signal, and the desktop context menu renders a "moves from" affordance when a prior holder exists, exercised by an automated test. The gap is coverage, not existence: `pin_options` — the descriptor mechanism that supplies more than one candidate pin for a given `(R, role)` — exists for exactly two of roughly nineteen resource roles on one of two supported boards (ESP32 `uart1`/`uart2` tx/rx only; the second board has no `pin_options` entries at all, and ESP32's SPI, I2C, GPIO, PWM, and ADC resources have none). The mechanism functions correctly where data exists for it; the data exists for a small minority of resources.

5. **MBD type compatibility is enforced only client-side.** `graph_editor.html`'s `expectedTypeMap` filters which resources are offered per node type in the UI, but `pin_conflicts.js`'s graph-level validation (`:258–337`) checks only that a `hardwareResource` string parses and does not collide on a physical pin — it does not check that the resource's `type` matches what the node type expects. `mbd/schema/graph.schema.json`'s node definitions do not constrain `params.hardwareResource` by pattern either. A `hardwareResource` value inconsistent with its node's expected type (e.g., a GPIO resource bound to a UART input node) can reach the persisted project without being rejected, provided it does not collide with another assignment. This is the structural asymmetry referenced in Section I: the hardware layer treats resource type as load-bearing for its checks; the graph layer treats it as a UI hint.

6. **Two node types reference a resource type that does not exist.** `expectedTypeMap` names `'encoder'` and `'motor'` as expected types for `EncoderInput`/`MotorOutput` nodes, but `pin_conflicts.js`'s `buildResources()` never produces either type (`:19–51`). These two dropdowns cannot be populated from any real project data — an aspirational vocabulary entry with no corresponding producer elsewhere in the system.

7. **Macro-name sanitization is implemented three times, independently.** `sanitizeMacro()` in `project_defines.js` (`:35–37`), an inline uppercase/replace pattern in `gen_board_config.js` (`:70`), and a further variant in `project_defines.js` (`:24–31`) each convert an arbitrary identifier fragment into a valid C macro token, with differing edge-case behavior (underscore collapsing, strip vs. replace). They agree on the inputs currently exercised, but there is no single shared "tag → macro" function used by all three sites.

---

## V. Comparison with Existing Toolchains

### A. STM32CubeMX

CubeMX's closest analogue to a semantic tag is the **`GPIO_Label`** (user label) mechanism: a designer assigns a label to a pin in the graphical Pinout view, and CubeMX's code generator emits exactly two macros from it, `<Label>_Pin` and `<Label>_GPIO_Port` [1], [2], which application code and HAL calls (e.g. `HAL_GPIO_WritePin(<Label>_GPIO_Port, <Label>_Pin, ...)`) then reference directly; regenerating the project after a pin reassignment redefines the macros without requiring application-code changes [1]. Within its scope, this is a tighter single-tag model than HyprAccel's: there is exactly one user-facing string and it is used unchanged (modulo two fixed suffixes) by every consumer of that pin.

The scope, however, is GPIO only. For peripherals — USART, SPI, I2C, timers — CubeMX generates a numbered instance handle tied to the MCU's fixed peripheral enumeration (e.g. `huart1`, `hspi2`, `htim3`), and application code addresses the peripheral by that handle rather than by any role-based or semantic label. A "one canonical tag" description of CubeMX would therefore only be accurate for its GPIO subset; for bus peripherals it uses vendor/instance numbering, which is a *weaker* abstraction on the semantic axis than HyprAccel's `uart.uart0` resource identifier — `uart.uart0` states what the resource is, whereas `huart1` states only which numbered peripheral block it is.

### B. Simulink / Embedded Coder / HDL Coder

Embedded Coder has no equivalent of a single semantic hardware-resource identifier that survives from model to generated code to runtime. Peripheral access in generated code is routed through vendor- or board-specific **device driver blocks**, delivered via hardware support packages, each generated independently per platform [3], [4]. Where no ready-made driver block exists for a peripheral, MathWorks' documented path is to hand-write the C driver and integrate it into the model via the **Legacy Code Tool** (or C Caller blocks / S-Functions), which generates a Simulink block from an existing C function signature [4] — the inverse of HyprAccel's descriptor-to-code direction: in this path, code is the source of truth and the model artifact is derived from it, not the other way around. Naming inside generated code is driven by Simulink signal and block names, which serve generated-code readability (variable and comment names) rather than functioning as a validated, cross-layer hardware-resource key.

**Net comparison finding.** Neither reference tool implements what this paper's Section IV-A identifies as HyprAccel's verified invariant: an identifier that is simultaneously (i) derived from a single hardware descriptor, (ii) checked for conflicts at build time against a separately authored visual configuration, and (iii) resolved by a runtime lookup on the target device. CubeMX achieves (i) and implicitly (iii) for GPIO only, with no equivalent of (ii) — CubeMX's pin conflict resolution is enforced by its own GUI at configuration time, not by comparing an independently stored graph artifact against the descriptor. Simulink/Embedded Coder achieves none of the three at the level of a semantic hardware-resource identifier.

**Table II. Comparison with reference toolchains**

| Property | HyprAccel | STM32CubeMX | Simulink / Embedded Coder |
|---|---|---|---|
| Hardware-resource identity | Two grains: *R* (`type.instance`), *F* (`type.instance.role`); not unified | `GPIO_Label` (GPIO only); numbered handle (`huart1`) for peripherals | None; driver blocks / Legacy Code Tool wrap peripheral access |
| Source of identity | Reconstructed from `boards.yaml` at every layer (no stored flat string) | User-entered label (GPIO) / fixed vendor enumeration (peripherals) | Model/block naming (readability only) or hand-written C |
| Propagation into generated code | Macro encoding, 3 independent sanitizers (Section IV-B.7) | `<Label>_Pin` / `<Label>_GPIO_Port` macros (GPIO); handle name (peripherals) | Generated variable/function names from block/signal names |
| Graphical binding | MBD graph `hardwareResource` field (*R*); type-checked client-side only | Pinout view label assignment; GUI-enforced at design time | Driver block parameters; no descriptor cross-check |
| Conflict validation | Build-time, `pin_conflicts.js`, graph vs. descriptor | GUI-time, within CubeMX itself | Not applicable (no shared identifier to validate) |
| Runtime resolution | Table lookup (*R* only) and compile-time macros (SDK) | Compile-time macro substitution only | N/A — generated code calls driver-block functions directly |
| Identifier persistence across regeneration | *R* stable; *F* reconstructed per generation, not stored | Stable if label unchanged | Stable if block/signal name unchanged |

---

## VI. Discussion: Novelty versus Engineering Quality

### A. What is implemented cleanly

The resource-identifier invariant at the *R* grain —

$$\texttt{boards.yaml} \to \texttt{pin\_conflicts.js} \to \texttt{hardwareResource} \to \text{SDK resolution}$$

— is consistently reconstructed, cross-validated at build time, and used as a live runtime lookup key, and it is covered by automated tests at the conflict-checking layer. This is a genuine engineering-quality property of the codegen/SDK path specifically. It does not extend uniformly to the rest of the system: three independent macro-sanitization implementations, a duplication unified only one day before this audit, and a graph editor whose type safety is enforced only in client-side JavaScript are all counterexamples to describing the codebase as uniformly "clean." The word applies to the pin-conflict path; it does not apply to the graph-editor-to-hardware-setup binding path.

### B. What may constitute an architectural contribution

The evidence in Sections III–V does not support the claim "one tag, one architecture" — Section IV-B.1 alone falsifies it. The defensible, narrower claim is:

> HyprAccel demonstrates a board-descriptor-derived semantic resource-identifier convention that is independently reconstructed and kept consistent across configuration layers, cross-validated at build time between the hardware descriptor and a separately authored visual (MBD) configuration, and resolved at runtime by the SDK against the same descriptor.

What makes this narrower claim potentially of interest, independent of code-quality judgments: the identifier is derived from a single hardware descriptor rather than invented per layer; it participates in build-time conflict checking against an independently authored artifact (the MBD graph); it is exposed in the visual/MBD hardware-binding surface; and it is resolved by a runtime lookup on the target device — with the same identifier value serving all four roles for a given resource, at the *R* grain. Neither reference tool examined in Section V combines these four properties for the same identifier: CubeMX's label is design-time-only for GPIO and has no independent-artifact cross-check; Simulink has no comparable resource identifier at all. This should be presented as a claim about combining four already-individually-known techniques (semantic labeling, descriptor-driven codegen, design-time cross-validation, runtime resolution) around one identifier value, not as a claim of a previously unknown mechanism.

---

## VII. Limitations and Future Work

1. Store the flattened resource identifier as an explicit value in the hardware schema (e.g. a generated or schema-derived `id` field per resource) rather than reconstructing it independently at each of the four current sites.
2. Define one formal identifier grammar covering both *R* and *F*, and specify which layers are permitted to consume which grain.
3. Separate resource identity from resource role explicitly in all data structures that currently conflate them via string concatenation (Stage 4, Stage 6).
4. Centralize the tag-to-macro encoding into one shared function used by `gen_board_config.js`, `project_defines.js`, and any future codegen module, replacing the three current independent implementations (Section IV-B.7).
5. Add schema- or backend-level validation of MBD `hardwareResource` type compatibility, so that `pin_conflicts.js` or the project schema — not only client-side JavaScript — rejects a resource/node-type mismatch (Section IV-B.5).
6. Extend the `pin_options` mechanism from its current two-of-nineteen-roles, one-of-two-boards coverage toward a general resource-to-candidate-pins model, or explicitly document that most resources are expected to remain single-pin.
7. Eliminate the possibility of a recurrence of Section IV-B.3's duplication by adding a regression test that fails if header-injection logic diverges between the materialize and `/api/generate` code paths.
8. Add automated consistency tests spanning the full chain `boards.yaml → codegen → MBD → SDK`, asserting that the *R* identifier reconstructed at each layer for a fixed descriptor is byte-identical, to catch drift before it reaches the state described in Section IV-B.2.
9. Resolve the `encoder`/`motor` resource-type mismatch (Section IV-B.6) by either implementing the corresponding resource producers in `pin_conflicts.js` or removing the unreachable entries from `expectedTypeMap`.
10. Evaluate the identifier convention's robustness on additional boards and resource categories beyond the two currently supported, particularly ones exercising `pin_options` more broadly.

---

## VIII. Conclusion

This audit does not validate the strong hypothesis that HyprAccel uses one canonical hardware-resource tag string, defined once and reused unchanged by every layer from the hardware descriptor to the runtime firmware. Tracing a concrete resource through seven implementation stages shows two related but structurally incompatible identifier grains in concurrent use — a two-part resource identifier and a three-part role-qualified identifier — with the SDK's own runtime parser rejecting the latter as invalid input, and a desktop-side module whose sole purpose is converting between the two.

What the audit does validate is a narrower, still substantive invariant: the two-part resource identifier is independently but consistently reconstructed from the hardware descriptor by every layer examined, is compared as literal string data — not merely by convention — between the MBD graph and the hardware descriptor at build time, and is resolved by the same grain in the SDK's runtime lookup table. This invariant is not matched in full by either STM32CubeMX, whose comparable single-tag mechanism is scoped to GPIO alone, or Simulink/Embedded Coder, which has no comparable hardware-resource-identity concept.

The architecture is therefore more accurately described as a semantic resource-identity *convention* with cross-layer, cross-artifact validation at one identifier grain, rather than as a fully canonical tag-first architecture. This is a more restrictive claim than the one it replaces, and it is the one the implementation evidence in Sections III–V supports.

---

## References

[1] STMicroelectronics Community, "User labels for peripherals," STM32CubeMX MCUs forum. [Online]. Available: https://community.st.com/t5/stm32cubemx-mcus/user-labels-for-peripherals/td-p/329403

[2] STMicroelectronics Community, "What is the purpose of labeling pins in the MX program if you cannot use the labels in your code?" STM32CubeMX MCUs forum. [Online]. Available: https://community.st.com/t5/stm32cubemx-mcus/what-is-the-purpose-of-labeling-pins-in-the-mx-program-if-you/td-p/106836

[3] MathWorks, "Embedded Coder — Generate C and C++ code from Simulink and MATLAB." [Online]. Available: https://www.mathworks.com/products/embedded-coder.html

[4] MathWorks, "Device Driver Blocks," Embedded Coder Documentation. [Online]. Available: https://www.mathworks.com/help/ecoder/device-driver-blocks.html
