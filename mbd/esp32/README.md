# ESP32 live-reflash target (MBD-T9)

This PlatformIO project targets an ESP32 DevKit V1-style board with an
ESP32-WROOM-32 module (`esp32dev`). Its `generated/` directory is deliberately
ignored: `/api/build` materializes the current graph C, SDK C sources, and a
small Arduino runtime wrapper there before PlatformIO compiles it.

The generated `graph.c` remains the computation and publish implementation;
the wrapper only initializes serial output and calls its generated init/step
functions periodically.

Required local setup:

```sh
# Run from the repository root.
python3 -m venv .venv-platformio
.venv-platformio/bin/pip install platformio
export HYPRACCEL_PLATFORMIO="$PWD/.venv-platformio/bin/pio"
```

Manual build/flash after using the editor endpoint:

```sh
cd mbd/esp32
pio run
pio run --target upload --upload-port /dev/serial/by-id/…
pio device monitor --port /dev/serial/by-id/… --baud 115200
```

After a flash, the monitor must show all of these generated runtime markers:

```
HYPRACCEL_MBD_T9_READY
HYPRACCEL_GRAPH_ID=<current graph id>
HYP_PUBLISH topic=<current Publish node topic> value=<computed value> bytes=4
```

The editor's **Verify** action reads the serial monitor for ten seconds and
requires the same markers. It deliberately does not call a successful upload a
runtime success until those markers arrive.

The preferred server configuration is:

```sh
HYPRACCEL_ESP32_PORT=/dev/serial/by-id/… npm start
```

If no port is configured, the server detects a single `/dev/serial/by-id`,
`/dev/ttyUSB*`, or `/dev/ttyACM*` candidate. It refuses to guess when multiple
candidates exist.
