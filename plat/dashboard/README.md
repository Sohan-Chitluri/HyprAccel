# HyprAccel telemetry dashboard (PLAT-T1)

This dependency-light dashboard streams real CORDIC values and latency from
the SDK's current software route. It launches a C feeder that calls
`hyp_init()`, `hyp_route(HYP_OP_CORDIC_SINCOS, HYP_TARGET_SOFTWARE)`, and
`hyp_compute()`; a small Node.js standard-library server relays those samples
to the browser through Server-Sent Events.

```sh
make -C plat/dashboard run
# Open http://localhost:8080
```

The FPGA card is intentionally labelled **simulated placeholder**. Its latency
is not hardware data and must be replaced with a real RTL-T4 driver measurement
when that path is available.
