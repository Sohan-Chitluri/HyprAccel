# Generated-Code Complexity: HyprAccel and Simulink + Embedded Coder

## Purpose and conclusion

This note compares the generated C emitted by the current HyprAccel examples
with a commonly repeated **general industry scale** for automotive control
modules generated with Simulink + Embedded Coder: roughly **30,000–40,000
lines of C**.  It is deliberately **not** a claim that the two outputs are
equivalent, or that HyprAccel is 1,000+ times simpler for the same product.

For the two example graphs currently shipped in this repository, HyprAccel
generates **21 physical lines of C per graph**.  These are single
CORDIC-to-publish chains.  The 30,000–40,000-line figure is context for a
**full vehicle control module**, which normally includes many algorithms,
states, interfaces, scheduling and integration concerns.  A line-count ratio
between those scopes would be numerically true but technically misleading.

## Measured HyprAccel output

Measurement was made on the committed examples in `mbd/schema/examples/`,
using the current generator without post-processing:

```sh
node mbd/codegen/graph_to_c.js \
  mbd/schema/examples/cordic-to-publish.graph.json /tmp/cordic-to-publish.c
wc -l -c /tmp/cordic-to-publish.c

node mbd/codegen/graph_to_c.js \
  mbd/schema/examples/cordic-hw-to-publish.graph.json /tmp/cordic-hw-to-publish.c
wc -l -c /tmp/cordic-hw-to-publish.c
```

| Example graph | Route selected in generated C | Physical C lines (`wc -l`) | Bytes (`wc -c`) |
| --- | --- | ---: | ---: |
| `cordic-to-publish.graph.json` | `HYP_TARGET_SOFTWARE` | 21 | 660 |
| `cordic-hw-to-publish.graph.json` | `HYP_TARGET_HARDWARE` | 21 | 666 |

Both outputs contain one include, `hyp_graph_init()`, and one step function.
The step function converts the angle, selects a CORDIC route, calls
`hyp_compute()`, extracts the sine result, and publishes it.  The hardware
variant adds no generated control logic; it changes the target enum passed to
`hyp_route()`.

This count is intentionally limited to the generated `.c` file.  It excludes
the HyprAccel SDK headers and implementations, the CORDIC implementation or
FPGA image, transport implementation, board support, build files, tests, and
the generator itself.  It is therefore a measure of *graph-specific emitted
glue code*, not total deployable firmware size.

## What the automotive figure means—and does not mean

The 30,000–40,000-line range is used here as a general, commonly cited
industry planning figure for generated C in a full automotive control module;
it is not attributed to a particular OEM, ECU, Simulink model, configuration,
or release.  It should not be read as an Embedded Coder guarantee or as a
measured baseline for this repository.

That distinction matters because production automotive code-generation
workflows may cover complete ECU application architectures: AUTOSAR mapping,
multiple control domains, target-specific integration, verification, and
functional-safety activities.  MathWorks describes Embedded Coder use in
production automotive ECUs at that system scale and notes that generated code
is integrated with target-specific APIs; it does not publish a universal
line-count expectation for an individual model. [MathWorks: Automotive Code
Generation](https://www.mathworks.com/solutions/embedded-code-generation/production-code-automotive-ecu.html)

The fair comparison basis is consequently:

| Dimension | HyprAccel measurement | Automotive industry context |
| --- | --- | --- |
| Unit being discussed | One CORDIC-to-actuator/telemetry chain | A full vehicle control module |
| Current functional scope | One numeric input, one CORDIC operation, one published value | Many functions and interfaces; exact content varies by programme |
| Count supplied here | 21 generated C lines per shipped example | Commonly cited 30,000–40,000 generated C lines |
| Valid inference | The present HyprAccel graph-to-C path is small and inspectable | Large automotive modules can yield much larger generated artifacts |
| Invalid inference | Equal feature coverage, equal safety case, equal runtime cost, or equivalent code quality | Any direct size or quality ranking against the 21-line examples |

## Complexity comparison beyond line count

Line count is a coarse proxy.  For this repository's examples, the generated
control-flow complexity is also small: each step executes a straight-line
sequence with no generated branch, loop, state machine, scheduler, lookup
table, or error-handling path.  The non-trivial computation and routing live
behind SDK calls, so their complexity must be assessed separately.

Conversely, a full automotive module's generated size can reflect model
content *and* code-generation configuration: data types, calibration data,
state logic, diagnostics, integration wrappers, target APIs, and AUTOSAR or
safety-related artifacts.  Embedded Coder is designed to customize code and
generation tools for a project or organization, another reason its output
size cannot be inferred from a generic range. [Embedded Coder
documentation](https://www.mathworks.com/help/ecoder/index.html)

## Reproducibility and update rule

The two figures above are a snapshot of the current generator and example
graphs.  Re-run the commands in **Measured HyprAccel output** whenever either
changes.  If a future comparison needs to be quantitative, generate a
Simulink/Embedded Coder model with the same one-input, CORDIC-or-sine,
one-output behavior; count the same artifact class (generated source only,
or complete generated build tree) on both sides; and publish the model,
configuration, target, and tool versions.  Until then, this document makes a
scope comparison, not a like-for-like benchmark.
