#!/usr/bin/env bash
# compile_esp32.sh — compile-only ESP32 check for the SDK against a given
# generated hyp_board_config.h. No hardware needed.
#
# Uses the same PlatformIO build the web editor's /api/compile runs
# (mbd/editor/server.js: `pio run --project-dir <dir>` on an espressif32 /
# esp32dev / arduino project whose src_dir holds the SDK sources plus the
# generated header). Here that project is assembled in a temp dir from any
# header path, so desktop-generated headers can be checked too.
#
# Usage:
#   sdk/test/compile_esp32.sh <path/to/hyp_board_config.h> [...more headers]
#   sdk/test/compile_esp32.sh --cases     # web-built, desktop-built, nonstandard-frequency
#
# PlatformIO is found like server.js platformioCommand():
#   $HYPRACCEL_PLATFORMIO, else <repo>/.venv-platformio/bin/pio, else `pio` on PATH.
#
# One-time local setup (NixOS, from the repo root; first build downloads the
# espressif32 platform + xtensa toolchain into ~/.platformio, needs network):
#   nix develop                      # flake devShell provides python3
#   python3 -m venv .venv-platformio
#   .venv-platformio/bin/pip install platformio
#   exit
#   sdk/test/compile_esp32.sh --cases
# The downloaded toolchain binaries are generic-Linux; on NixOS they need
# programs.nix-ld.enable = true (already enabled on this machine).
#
# Exit codes: 0 all headers compiled, 1 a compile failed, 2 bad usage, 3 PlatformIO not found.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [[ -n "${HYPRACCEL_PLATFORMIO:-}" ]]; then
    PIO="$HYPRACCEL_PLATFORMIO"
elif [[ -x "$REPO_ROOT/.venv-platformio/bin/pio" ]]; then
    PIO="$REPO_ROOT/.venv-platformio/bin/pio"
else
    PIO="pio"
fi
if ! command -v "$PIO" >/dev/null 2>&1; then
    echo "PlatformIO not found (tried \$HYPRACCEL_PLATFORMIO, .venv-platformio/bin/pio, pio on PATH)." >&2
    echo "See the setup steps at the top of $0." >&2
    exit 3
fi

if [[ $# -eq 0 ]]; then
    sed -n '2,30p' "$0" >&2
    exit 2
fi

if [[ "$1" == "--cases" ]]; then
    set -- \
        "$REPO_ROOT/mbd/editor/test/fixtures/codegen_baseline/materialize_web_uart.h" \
        "$REPO_ROOT/sdk/test/fixtures/board_config_pinfunc.h" \
        "$REPO_ROOT/sdk/test/fixtures/board_config_pinfunc_nonstd.h"
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/hypraccel-esp32-compile.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
# Share one build cache across headers so the framework is compiled once.
export PLATFORMIO_BUILD_CACHE_DIR="${PLATFORMIO_BUILD_CACHE_DIR:-$WORK/cache}"

failures=0
for header in "$@"; do
    if [[ ! -f "$header" ]]; then
        echo "FAIL $header — file not found" >&2
        failures=$((failures + 1)); continue
    fi
    project="$WORK/project"
    rm -rf "$project"; mkdir -p "$project/generated"

    # Same platformio.ini shape as server.js projectPlatformioIni('esp32').
    cat > "$project/platformio.ini" <<'INI'
[platformio]
src_dir = generated

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
build_flags =
    -I generated
    -Wall
INI

    # Same SDK file set server.js materializeEsp32 copies into generated/.
    for file in hyp_esp32.c hyp_router.c hyp_cordic_ref.c hyp_cordic_ref.h hyp_esp32_hw.cpp hyp_pid.c hyp_encoder.c; do
        cp "$REPO_ROOT/sdk/src/$file" "$project/generated/"
    done
    cp "$REPO_ROOT/sdk/include/hyprccel.h" "$REPO_ROOT/sdk/include/hyp_esp32_hw.h" "$project/generated/"
    cp "$header" "$project/generated/hyp_board_config.h"

    # Minimal runtime wrapper: the SDK init path only (no generated graph).
    cat > "$project/generated/main.cpp" <<'CPP'
#include <Arduino.h>
#include "hyprccel.h"
#include "hyp_esp32_hw.h"

void setup()
{
    Serial.begin(115200);
    hyp_esp32_hw_init();
}

void loop()
{
    delay(1000);
}
CPP

    log="$WORK/$(basename "$header").log"
    if "$PIO" run --project-dir "$project" >"$log" 2>&1; then
        warnings=$(grep -c 'warning:' "$log" || true)
        echo "PASS $header (compiled for esp32dev, $warnings compiler warnings)"
    else
        echo "FAIL $header — compile errors:" >&2
        grep -E 'error:|Error' "$log" | head -20 >&2 || tail -20 "$log" >&2
        failures=$((failures + 1))
    fi
done

[[ $failures -eq 0 ]] || exit 1
