# MBD graph-to-C codegen

Generate C from an MBD graph with:

```sh
node mbd/codegen/graph_to_c.js <graph.json> <output.c>
```

The V1 SDK currently exposes CORDIC SINCOS and raw publication, so this
generator supports `CordicOp` (`sin`, `cos`, or `sincos`) and `Publish` nodes.
It emits `hyp_graph_init()` and one graph step function that accepts declared
numeric graph inputs. The `CordicOp.params.implementation` selector maps
directly to the SDK's `hyp_target_t`: `hardware` emits
`HYP_TARGET_HARDWARE`, while `auto` and `software` emit
`HYP_TARGET_SOFTWARE` until the routing layer gains automatic target selection.

Example:

```sh
node mbd/codegen/graph_to_c.js \
  mbd/schema/examples/cordic-to-publish.graph.json /tmp/cordic_graph.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -Isdk/include /tmp/cordic_graph.c
```
