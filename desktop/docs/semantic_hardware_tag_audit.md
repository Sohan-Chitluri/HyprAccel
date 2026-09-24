# Audit: Is HyprAccel a "Semantic Hardware-Tag-First" Architecture?

Status: internal audit, source material for a paper draft. Not a marketing document — findings that weaken the claim are reported as found.

Repo: HyprAccel, audited at commit range up to `43d1a85` (working tree includes uncommitted changes to `boards/boards.yaml`, `boards/codegen/*`, `desktop/src/board/board_catalog.{h,cpp}`, `desktop/src/config/tests/pin_assignment_model_test.cpp` as of 2026-09-24).

---

## (a) The claim, stated precisely

> Every hardware resource (a pin, a peripheral, a bus signal) is given one canonical semantic tag — e.g. `spi.hspi`, `uart.uart0`, or a role-qualified form like `uart.uart0.tx` — at the hardware-descriptor layer (`boards/boards.yaml`), and every downstream layer (codegen, the SDK runtime, the desktop pin/clock UI, the MBD graph editor, conflict checking) reads and writes *that same string*, rather than inventing its own naming or indexing scheme.

This is a strong claim with two parts that must both hold:
1. **One tag format.** There is a single string shape used everywhere.
2. **Passed through unchanged.** Downstream layers consume that literal string rather than re-deriving an equivalent-but-different identifier.

The audit below shows part 2 holds in the one place that matters most for correctness (conflict detection), but part 1 does **not** hold globally — there are in fact two related but distinct tag grains in concurrent use, and several layers re-encode the tag into macro names rather than carrying the string itself.

---

## (b) End-to-end trace, with evidence

### Hop 0 — origin: `boards/boards.yaml`

Hardware is declared as a nested map, not as flat tag strings:

```yaml
uart:
  uart0: { tx: "GPIO1", rx: "GPIO3" }
  uart1: { tx: "GPIO10", rx: "GPIO9" }
  uart2: { tx: "GPIO17", rx: "GPIO16" }
```
(`boards/boards.yaml:173-176`, ESP32 board)

There is **no literal tag string `"uart.uart0.tx"` anywhere in boards.yaml**. The tag is implicit in the YAML path (`pins.uart.uart0.tx`). Every downstream consumer that wants a flat tag string has to construct it itself from `type`, `instance`, `role` — i.e., the "canonical tag" is a convention several independent pieces of code agree to reconstruct identically, not a value that is defined once and threaded through as data. This matters for the claim: the tag is canonical by *convention*, not by *single definition*.

An additive `pin_options` block extends the same instance/role addressing with alternate physical pins, e.g.:

```yaml
pin_options:
  uart:
    uart1:
      tx: ["GPIO10", "GPIO25", "GPIO26"]
```
(`boards/boards.yaml:197-204`) — present only for ESP32 `uart1`/`uart2`. THEJAS32 has no `pin_options` at all; ESP32's `spi`, `i2c`, `gpio`, `pwm`, `adc` resources have none either.

### Hop 1 — `boards/codegen/gen_board_config.js` (base header codegen)

Reconstructs the tag components from the YAML path and encodes them into a **macro name**, not a string value:

```js
const macro = `HYP_RESOURCE_UART_${inst.toUpperCase()}`;              // HYP_RESOURCE_UART_UART0
out += `#define ${macro}_${role.toUpperCase()}_PIN ${num}  /* ${pin} */\n`; // HYP_RESOURCE_UART_UART0_TX_PIN 1  /* GPIO1 */
```
(`boards/codegen/gen_board_config.js:161-172`)

The tag is present only as a *comment* (`/* GPIO1 */` — actually the pin, not the tag) and as the shape of the macro identifier (`UART_UART0_TX`). At this hop the tag has already been irreversibly transformed: `.` → `_`, lowercase → uppercase. This is a mechanical, information-preserving transform (the C preprocessor can't hold dots in identifiers, so some transform is unavoidable), but it is a transform, and it happens independently in every codegen module that touches a resource id — there is no single "tag → macro" function reused across the codebase (see Hop 3).

### Hop 2 — `boards/codegen/pin_conflicts.js` (conflict logic)

This is the one place the flat 2-part tag genuinely is constructed once and reused as *data*, not just as a naming convention:

```js
resources[`${type}.${instance}`] = { id: `${type}.${instance}`, type, instance, roles, options };
```
(`boards/codegen/pin_conflicts.js:33`) — builds keys like `"uart.uart0"`, `"spi.hspi"`.

And on the graph side:
```js
const resourceId = node && node.params && node.params.hardwareResource;
```
(`pin_conflicts.js:268`) — reads the *same* 2-part string back off a graph node's `hardwareResource` param and compares it by string equality against the resources map built from `boards.yaml`. This is genuine tag reuse: `boards.yaml`'s `type.instance` shape and the MBD graph node's `hardwareResource` field are checked against each other as literal strings with no re-encoding. **This is the strongest evidence for the claim in the whole codebase.**

Note the grain, though: this tag is `type.instance` (2 parts — e.g. `"uart.uart0"`), not `type.instance.role` (3 parts — e.g. `"uart.uart0.tx"`). The *role* (`tx`/`rx`/`sck`/…) is carried as a sibling field (`a.role`), not baked into this tag. That distinction becomes important at Hop 5.

### Hop 3 — `boards/codegen/project_defines.js` (per-project header lines)

This module independently reconstructs macro names from assignment data, and it does so with **three different keying schemes in the same file**:

1. `HYP_PIN_<node>_<ROLE>` — keyed by the **graph node's own name**, not by the resource tag:
   ```js
   pinDefines.push(`#define HYP_PIN_${macroBase}_${a.role.toUpperCase()} "${a.pin}"  /* ${a.node} → ${a.resource}.${a.role} */`);
   ```
   (`project_defines.js:74`, `macroBase = assignmentMacroBase(a)` derived from `a.node`)

2. `HYP_PERIPH_<node>_<RESOURCE>` — also node-keyed (`project_defines.js:75-78`).

3. `HYP_PINFUNC_<PIN>_<FUNC>` — keyed by the **physical pin**, where `FUNC` is (finally) a sanitized form of the 3-part tag:
   ```js
   function pinFunctionFor(a) {
       const type = String(a.resource || '').split('.')[0];
       if (type === 'gpio' && a.role === 'gpio') return 'gpio';
       if (type === 'pwm' && a.role === 'output') return 'pwm';
       if (type === 'adc' && a.role === 'input') return 'adc';
       return `${a.resource}.${a.role}`;      // e.g. "uart.uart0.tx"
   }
   ```
   (`project_defines.js:126-132`, used at `:145`)

So within a **single file**, the "same" hardware assignment is addressed three different ways: by graph node identity, by physical pin, and (only in the third case) by something resembling the 3-part tag — and only after sanitization (`sanitizeMacro`, uppercased, non-alnum stripped) that is irreversible (`uart.uart0.tx` and a hypothetical `uart.uart0_tx` would collide). The 3-part tag string itself (`${a.resource}.${a.role}`) is real and does flow from the assignment object into this function, but it is immediately shredded into a macro-name fragment, never stored or passed on as a string elsewhere in the generated header.

**Historical duplication (user-flagged gap, now confirmed and fixed):** the module's own header states it "Replaces server.js injectHardwareHeader() and the inline copy in POST /api/generate." Commit `3b4b488` ("feat(codegen): feed desktop pin/clock config into board codegen via one shared path", 2026-09-23) is the fix — its message states plainly: *"the web server had two copies of the header-injection code (injectHardwareHeader for materialize, an inline copy in /api/generate) that had already drifted."* This means until 2026-09-23, the same tag→macro logic was implemented twice and had already diverged — the opposite of "one canonical tag reused everywhere." The unification is one day old relative to this audit.

### Hop 4 — SDK runtime: `sdk/src/hyp_esp32_hw.cpp`

Two distinct mechanisms exist here, and they disagree with each other on tag grain:

**(4a) Compile-time macros.** Peripheral init blocks are gated on `HYP_RESOURCE_*` / `HYP_PINFUNC_*` macros produced at Hops 1 and 3, e.g.:
```c
#if defined(HYP_RESOURCE_PWM_GPIO32) && (!defined(HYP_SDK_PINFUNC_MODE) || defined(HYP_PINFUNC_GPIO32_PWM))
```
(`sdk/src/hyp_esp32_hw.cpp:897`) — these are preprocessor identifiers, resolved at compile time. No tag string exists at runtime here at all; the "tag" has been fully compiled away into conditional compilation.

**(4b) Runtime resource table.** `hyp_resource_pin_table[]` is a genuine runtime lookup table keyed by string:
```c
{ "adc.GPIO32", HYP_RESOURCE_ADC_GPIO32_PIN },
{ "gpio.GPIO1", HYP_RESOURCE_GPIO_GPIO1_PIN },
```
(`sdk/src/hyp_esp32_hw.cpp:~1075-1120`) — and for bus types, resolved via `canonical_instance_equals(instance, "uart0")` (`:1232` etc.) rather than table lookup.

Critically, `parse_resource_id()` — the function that splits an incoming resource-id string at runtime — **explicitly rejects any string containing a second dot**:
```c
const char *dot = strchr(resource_id, '.');
if (!dot || dot == resource_id || dot[1] == '\0' || strchr(dot + 1, '.') != NULL) return -1;
```
(`sdk/src/hyp_esp32_hw.cpp:1032`)

This means the SDK's only runtime string-based resource identifier is the **2-part** form (`type.instance`, matching Hop 2's `pin_conflicts.js` convention exactly — good agreement there). The **3-part** form (`type.instance.role`, e.g. `uart.uart0.tx`) that Hop 3's `pinFunctionFor()` constructs is a string the SDK's own parser is written to actively reject. The two tag grains are not just "different levels of the same thing" — one is a hard input-validation boundary against the other.

### Hop 5 — Desktop UI: `desktop/src/board/board_catalog.{h,cpp}`, `desktop/src/config/pin_assignment_model.cpp`

`board_catalog.cpp` parses `boards.yaml` into a `BoardCatalog` of `Board`/`Pin`/`Resource` structs, and folds `pin_options` into each `Pin::functions` list (`board_catalog.cpp:85-86, 137-147`) — functions are 3-part strings like `"uart.uart1.tx"`.

`pin_assignment_model.cpp` then has to **explicitly convert** between the 3-part "function" string (its own vocabulary, used for the pin-picker UI and the "moves from" logic) and the 2-part "resource id" + role pair (`pin_conflicts.js`'s vocabulary):

```cpp
ResourceRole resourceRoleFor(const QString &pin, const QString &function) {
    ...
    const auto parts = function.split('.');
    if (parts.size() == 3) return {parts[0] + "." + parts[1], parts[2]};
    return {};
}

QString functionForResourceRole(const QString &resource, const QString &role) {
    ...
    if ((type == "uart" || type == "spi" || type == "i2c") && resource.count('.') == 1 && !role.isEmpty())
        return resource + "." + role;
    return {};
}
```
(`desktop/src/config/pin_assignment_model.cpp:33-52`)

The existence of these two named conversion functions is itself the clearest single piece of evidence that "one tag" is aspirational rather than actual: two parts of the same desktop binary need a translation layer between two tag shapes that both claim to describe the same hardware resource.

### Hop 6 — MBD graph editor: `mbd/editor/src/graph_editor.html`, `mbd/editor/src/pin_config.html`

The graph editor's node inspector populates a "Canonical Hardware Resource" dropdown by fetching `hardware.resources` from the project server and filtering by an `expectedTypeMap`:
```js
const expectedTypeMap = {
  GPIOInput: ['gpio'], ADCInput: ['adc'], PWMOutput: ['pwm'],
  UARTInput: ['uart'], UARTOutput: ['uart'],
  EncoderInput: ['encoder'], MotorOutput: ['motor'],
  SensorInput: ['gpio', 'adc', 'uart'], ActuatorOutput: ['gpio', 'pwm', 'uart']
};
```
(`mbd/editor/src/graph_editor.html:1408-1419`)

It writes `node.params.hardwareResource` as the 2-part id (`resource.id`, `:1427`) — matching Hop 2's grain, good. But note `EncoderInput` and `MotorOutput` expect resource type `'encoder'` / `'motor'` — types that **do not exist** anywhere in `pin_conflicts.js`'s `buildResources()` (only `gpio`, `pwm`, `adc`, `spi`, `i2c`, `uart`, `accelerator` are ever produced, `pin_conflicts.js:19-51`). The dropdown for these two node types can therefore never be populated — a dead code path, not merely an inert feature but a broken one, silently.

---

## (c) Where the "one canonical tag" claim breaks down

1. **Two tag grains coexist and require explicit translation.** `type.instance` (2-part, used by `pin_conflicts.js`, the SDK runtime table, and `hardwareResource` on graph nodes) and `type.instance.role` (3-part, used as the desktop `Pin::functions` vocabulary and as `pinFunctionFor()`'s output). `pin_assignment_model.cpp:33-52` exists solely to convert between them. This is the single most damaging finding against the "one tag" claim: it is not that layers occasionally rename the tag, it's that **two different, incompatible tag shapes are both called canonical by different parts of the codebase**, and the SDK's own parser (`parse_resource_id`, Hop 4) is written to reject the 3-part shape outright.

2. **The tag is a convention, not a value.** `boards.yaml` never contains the string `"uart.uart0"` or `"uart.uart0.tx"` — every consumer reconstructs it from the YAML path (`type` key, `instance` key, `role` key). At least four independent reconstruction sites exist (`pin_conflicts.js:33`, `board_catalog.cpp` resource parsing, `project_defines.js:127-131`, `graph_editor.html` resource listing pulled from the server's own reconstruction). They currently agree, but nothing in the type system enforces that agreement — it is convention held together by test coverage, not by a shared constant or schema.

3. **UART settings were duplicated until yesterday.** Confirmed via git history: `mbd/editor/server.js` had two independent implementations of header injection (`injectHardwareHeader` for the materialize path, an inline copy in `POST /api/generate`) that had "already drifted" per the fixing commit's own message (`3b4b488`, 2026-09-23). For most of this project's history, the tag-to-macro pipeline was not even singular within one file, let alone across layers.

4. **"Moves from <pin>" is real but narrow, not inert.** Contrary to the premise that this feature is dead: `PinAssignmentModel::assign()` (`pin_assignment_model.cpp:83-105`) does enforce that a bus-signal function (3-part, `count('.') == 2`) is exclusive to one pin, evicting the previous holder and emitting `assignmentChanged`, and `pin_context_menu.cpp:90-92` does render "(moves from GPIOx)" when `otherHolderOf()` finds a prior holder. This is wired end-to-end and is exercised by `pin_context_menu_test.cpp::movesFromSuffixUsingModelApi()`. It is real. What's true is that it's **narrow**: `pin_options` (the only source of multiple candidate pins per tag) exists for exactly 2 of ~19 resource roles on 1 of 2 boards (ESP32 `uart1`/`uart2` tx/rx only; THEJAS32 has zero `pin_options`, and ESP32's spi/i2c/gpio/pwm/adc have none). So the tag-to-candidate-pins model is implemented and functions correctly, but almost every resource in the system today has exactly one candidate pin, making the feature's practical surface area small. Calling it "inert" overstates the gap; calling it "structurally load-bearing" would overstate the coverage.

5. **The MBD graph editor enforces port/resource-type compatibility only as client-side UI filtering, not as a backend or schema invariant.** `graph_editor.html`'s `expectedTypeMap` (`:1408-1419`) filters the dropdown so a `UARTInput` node is only shown `uart.*` resources — but this is JavaScript running in the property inspector. `pin_conflicts.js`'s Step 4 graph-level checks (`pin_conflicts.js:258-337`) validate that a `hardwareResource` string parses and doesn't collide on a physical pin with something else, but **never check that the resource's `type` matches what the node type expects.** Nothing stops a `hardwareResource` of `"gpio.GPIO4"` being saved on a `UARTInput` node via a direct `PATCH` to the project API (bypassing the dropdown); `pin_conflicts.js` would accept it as long as no other assignment claims `GPIO4`. `mbd/schema/graph.schema.json`'s `uartInputNode` definition does not appear to constrain `params.hardwareResource` to a `uart.*` pattern either (it is typed as a free string). This confirms the user's premise directly: the MBD graph editor's tags are structurally weaker than the hardware layer's tags — the hardware layer treats resource type as load-bearing (bus-completeness checks, `UART0_CONSOLE_PIN_REASSIGNED`), the graph layer treats it as a UI hint.

6. **`EncoderInput`/`MotorOutput` reference resource types that don't exist.** `expectedTypeMap` names `'encoder'` and `'motor'` as expected resource types (`graph_editor.html:1414-1415`), but `buildResources()` in `pin_conflicts.js` never produces a resource of either type — only `gpio`, `pwm`, `adc`, `spi`, `i2c`, `uart`, `accelerator` (`pin_conflicts.js:19-51`). These two node types can never have their hardware-resource dropdown populated from real data. This looks like an aspirational placeholder (future hardware categories) that was wired into the UI's type map before the corresponding resource type existed anywhere else — a small but concrete instance of a layer inventing a tag vocabulary the rest of the system doesn't share.

7. **Macro-name mangling is not centralized and is lossy.** `sanitizeMacro()` (`project_defines.js:35-37`), the inline `.toUpperCase().replace(/[^A-Za-z0-9_]/g, '_')` patterns in `gen_board_config.js` (`:70`), and equivalent logic in `project_defines.js:24-31` are three separately-written implementations of "turn an arbitrary string into a valid C macro fragment," each slightly different (one collapses repeated underscores, one doesn't; one strips vs. replaces). They happen to agree on the inputs actually exercised today, but there is no single canonical "tag → macro" function shared across `gen_board_config.js`, `project_defines.js`, and the SDK's `#define` names it consumes.

---

## (d) Comparison to reference tools

### STM32CubeMX

CubeMX's actual mechanism (per ST's own generated-code convention, confirmed via ST community documentation) is the **`GPIO_Label`**: a user assigns a semantic label to a pin in the graphical Pinout view (e.g. "BoardLED" for PC13), and CubeMX generates exactly two macros from it — `BoardLED_Pin` (→ `GPIO_PIN_13`) and `BoardLED_GPIO_Port` (→ `GPIOC`) — which HAL calls (`HAL_GPIO_WritePin(BoardLED_GPIO_Port, BoardLED_Pin, ...)`) then reference directly. [Source: ST community / deepbluembedded HAL GPIO guides.]

This is a genuine single-tag, reused-verbatim model — arguably tighter than HyprAccel's, because there is exactly one user-facing string and it appears unchanged (modulo the fixed `_Pin`/`_GPIO_Port` suffixes) everywhere downstream. The important caveat: **this applies only to GPIO pins.** For peripherals (USART, SPI, I2C, timers), CubeMX does not generate a semantic tag at all — it generates a numbered instance handle (`huart1`, `hspi2`, `htim3`) tied to the MCU's fixed peripheral numbering, and application code addresses the peripheral by that handle, not by a role-based tag. So CubeMX's "one canonical tag" claim would only be true for the GPIO subset; for buses it uses vendor/instance numbering, which is arguably a *weaker* abstraction than HyprAccel's `uart.uart0` resource-id convention (HyprAccel's tag is at least semantically typed — `uart.uart0` says what it is; `huart1` says only which numbered peripheral block it is).

`mbd/docs/three_lineages_comparison.md` describes HyprAccel's Pin Config screen as adopting "the visual pin-mapping philosophy" of CubeMX (`three_lineages_comparison.md:8`) — this framing (visual philosophy, not identical tag mechanism) holds up; the doc does not claim tag-format equivalence, so there's nothing to correct there.

### Simulink / Embedded Coder / HDL Coder

Embedded Coder has no equivalent of a single semantic hardware tag that survives from model to generated code. Peripheral access goes through vendor- or board-specific **driver blocks** (e.g. from a hardware support package), each independently generated; where no ready-made driver block exists, MathWorks' own guidance is to hand-write the C driver and wrap it via the **Legacy Code Tool**, which generates a Simulink block from a C function signature — i.e., the *code* is the source of truth and the block is generated from it, the reverse direction from HyprAccel's yaml→code flow. Naming in generated code is driven by Simulink signal/block names, which are for human readability in the generated comments and variable names, not by a resolved, validated hardware-resource identifier. ARM's CMSIS (referenced in search results as the closest thing to a standard) standardizes register and peripheral *names* at the vendor-SDK level, which Simulink code sits on top of, but that's a different, lower layer than what HyprAccel's tag addresses.

So the comparison point for the paper is: **HyprAccel's tag is closer to a novel contribution here than the CubeMX comparison suggests**, because Simulink genuinely has no equivalent hardware-resource-identifier concept that flows through model → generated code → runtime. `mbd/docs/three_lineages_comparison.md`'s Simulink section (`:11`) frames the analogy correctly — it claims HyprAccel replicates Simulink's "Graph → C" *workflow* philosophy, not its hardware-binding mechanism, and does not overclaim tag equivalence. No correction needed there either.

**Net comparison finding:** the existing internal docs (`three_lineages_comparison.md`, `complexity_comparison.md`) do not make false claims about CubeMX/Simulink internals — they're written at a level of abstraction (workflow philosophy, licensing/lock-in) that sidesteps the tag-naming question entirely. That's actually a gap in the *existing* docs for the paper's purposes: neither reference tool is compared on the tag-identity axis at all, so section (d) here is new material, not a correction of existing material.

---

## (e) What's novel vs. what's "implemented cleanly" — these are different claims

**Implemented cleanly (a quality claim, and only partially true):** The 2-part `type.instance` resource-id convention is consistently reconstructed and checked as data in exactly one place that matters for safety — `pin_conflicts.js` cross-referencing `boards.yaml`-derived resources against MBD graph nodes' `hardwareResource` fields, and the SDK's runtime table using the same grain. That is a real, working, tested (`pin_conflicts.test.js`) invariant. But "cleanly" is weakened by: three independent macro-mangling implementations (finding 7), a duplication that was only unified one day before this audit (finding 3), and a graph editor whose type-safety is UI-only, not schema- or backend-enforced (finding 5). "Clean" is the right word for the codegen→SDK pin-conflict path; it is the wrong word for the graph-editor→hardware-setup binding path.

**Novel (an architectural-contribution claim, independent of code quality):** What HyprAccel actually has that neither reference tool has is a **cross-layer resource identifier that is simultaneously human-readable, board-descriptor-derived, and used as a live runtime validation key** — `uart.uart0` is checked for conflicts at build time (`pin_conflicts.js`) *and* resolved to a physical pin at MCU runtime (`hyp_resource_pin_table`) *and* referenced by a visual graph node (`hardwareResource`) *and* rendered in a desktop pin inspector — from one YAML declaration. CubeMX's `GPIO_Label` is close for GPIO only and has no runtime resolution step (it's baked into `#define`s, full stop — there's no equivalent of HyprAccel's `parse_resource_id()`/`hyp_resource_pin_table` doing a *runtime* lookup rather than a compile-time substitution). Simulink has nothing comparable at all. So the novel claim should be scoped precisely: **not** "one tag everywhere" (demonstrably false per section c), but **"one board-descriptor-derived resource-id convention that is independently reconstructed, validated, and dynamically resolved across build-time, compile-time, and run-time layers, with build-time cross-checking between a visual graph and a physical pin map that neither CubeMX nor Simulink attempts."** That is a narrower, defensible, and more interesting claim than "one canonical tag" — and it survives a hostile review, because it doesn't require the reviewer to find only one counterexample (the 3-part-vs-2-part split, section c.1) to falsify it.

**Recommendation for the paper:** Do not claim "one tag, one architecture." Claim the narrower, true thing: a convention-based resource identifier, consistently reconstructed and cross-validated at the one hop that matters for safety (pin-conflict checking between graph and hardware config), with honestly reported gaps at the UI/type-safety layer and a historically recent (not yet battle-tested) unification of what used to be duplicated codegen logic.
