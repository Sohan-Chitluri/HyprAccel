# RTL-T1 verification result

The RTL source files in this directory were copied from
`/home/peskybird/Projects/cordic-accelerator-asic/rtl` (only a trailing EOF
newline differs). The regression vectors are generated deterministically by
`reference/cordic_golden/cordic_golden.py`.

Command run:

```sh
nix develop --command make -C rtl/tb test
```

Result: **failed, 10,000 of 10,000 vectors mismatched**. Comparison is exact
for `x_out`, `y_out`, `z_out`, and `overflow`; no tolerance or expected
value was changed.

First vector:

| field | value |
| --- | ---: |
| input `x_in, y_in, z_in, cfg_saturate` | `-5561, -4453, -5744, 0` |
| RTL output | `-5368, 4686, 5, 0` |
| golden output | `-5294, 4769, 26, 0` |

The copied source's pre-existing directed test constants also disagree with
the golden model for the 45-degree vector's residual `z` (`16` in the
directed RTL test, `29` from the golden model). This requires an upstream
arithmetic-contract decision; RTL-T1 remains blocked.
