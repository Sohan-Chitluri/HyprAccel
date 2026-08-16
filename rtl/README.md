# CORDIC RTL

This directory imports the generic SystemVerilog rotation-mode CORDIC core from
`cordic-accelerator-asic`. It is fixed at 16-bit signed Q4.12 and eight
registered CORDIC iterations. The public module is `cordic_top`.

The data interface is valid/ready; configuration uses the separate
`config_valid/config_ready` handshake. Reset is synchronous and active low.
No ASIC primitive is required.

Run the exact bit-level 10,000-vector regression from the repository root:

```sh
nix develop --command make -C rtl/tb test
```

Vectors are deterministically regenerated from
`reference/cordic_golden/cordic_golden.py`. The test checks `x_out`,
`y_out`, `z_out`, and `overflow` for exact equality.
