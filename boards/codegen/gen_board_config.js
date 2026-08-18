#!/usr/bin/env node

/**
 * gen_board_config.js — HyprAccel CORE-T3
 *
 * Generates a C header (hyp_board_config.h) from boards.yaml
 * for a specific target board.
 *
 * Usage: node gen_board_config.js <target_board> <path_to_boards.yaml> <out_dir>
 *
 * FW-P1..P6: Also emits per-signal pin macros so the runtime can resolve
 * physical pin numbers from generated configuration rather than hardcoded values.
 *
 * Emitted macro families:
 *   HYP_RESOURCE_SPI_HSPI                  — bus enabled (1)
 *   HYP_RESOURCE_SPI_HSPI_SCK_PIN   14    — physical GPIO number (int)
 *   HYP_RESOURCE_SPI_HSPI_MOSI_PIN  13
 *   HYP_RESOURCE_SPI_HSPI_MISO_PIN  12
 *   HYP_RESOURCE_SPI_HSPI_CS_PIN    15
 *   HYP_RESOURCE_I2C_I2C0_SDA_PIN   21
 *   HYP_RESOURCE_I2C_I2C0_SCL_PIN   22
 *   HYP_RESOURCE_UART_UART0_TX_PIN   1
 *   HYP_RESOURCE_UART_UART0_RX_PIN   3
 *   HYP_RESOURCE_PWM_GPIO25_PIN      25   — canonical output pin number
 *   HYP_RESOURCE_ADC_GPIO32_PIN      32   — canonical input pin number
 *   HYP_RESOURCE_GPIO_GPIO4_PIN       4   — canonical GPIO pin number
 */

const fs = require('fs');
const path = require('path');

/**
 * Extract a GPIO number from a string such as "GPIO14" -> 14.
 * Returns -1 when the format is not recognised.
 */
function gpioNum(pinStr) {
    if (!pinStr || typeof pinStr !== 'string') return -1;
    const m = pinStr.match(/^GPIO(\d+)$/i);
    return m ? parseInt(m[1], 10) : -1;
}

/**
 * boards.yaml indentation:
 *   0:  boards:
 *   2:    thejas32:
 *   4:      name: ...
 *   4:      pins:
 *   6:        gpio:
 *   8:          - "GPIO0"
 *   6:        spi:
 *   8:          hspi: { sck: "GPIO14", ... }
 *   4:      accelerators:
 *   6:        - "CORDIC"
 */
function parseSimpleYaml(content) {
    const lines = content.split('\n');
    const result = { boards: {} };
    let currentBoard = null;
    let currentCategory = null;   // board-level category (pins, accelerators, ...)
    let inPinsSection = false;    // true when inside pins:
    let pinSection = null;        // current pin sub-section (gpio, spi, i2c, uart, pwm, adc)

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].split('#')[0].trimEnd();
        if (line.trim().length === 0) continue;

        const indent = line.search(/\S/);
        const trimmed = line.trim();

        if (indent === 0 && trimmed === 'boards:') continue;

        // indent=2: board key
        if (indent === 2 && trimmed.endsWith(':')) {
            currentBoard = trimmed.slice(0, -1);
            result.boards[currentBoard] = {
                accelerators: [],
                pins: { gpio: [], spi: {}, i2c: {}, uart: {}, pwm: [], adc: [] }
            };
            currentCategory = null;
            inPinsSection = false;
            pinSection = null;
            continue;
        }

        // indent=4: board-level property or category
        if (indent === 4 && currentBoard) {
            if (trimmed === 'pins:') {
                currentCategory = 'pins';
                inPinsSection = true;
                pinSection = null;
                continue;
            }
            if (trimmed === 'accelerators:') {
                currentCategory = 'accelerators';
                inPinsSection = false;
                pinSection = null;
                continue;
            }
            // Other board-level properties
            inPinsSection = false;
            pinSection = null;
            if (trimmed.endsWith(':')) {
                currentCategory = trimmed.slice(0, -1);
            } else if (trimmed.includes(':')) {
                const colonIdx = trimmed.indexOf(':');
                const key = trimmed.slice(0, colonIdx).trim();
                let val = trimmed.slice(colonIdx + 1).trim();
                if (val.startsWith('"') && val.endsWith('"')) val = val.slice(1, -1);
                else if (val !== '' && !isNaN(Number(val))) val = Number(val);
                result.boards[currentBoard][key] = val;
            }
            continue;
        }

        // indent=6: accelerator list items OR pin sub-section headers
        if (indent === 6 && currentBoard) {
            if (currentCategory === 'accelerators' && trimmed.startsWith('- "')) {
                result.boards[currentBoard].accelerators.push(trimmed.slice(3, -1));
                continue;
            }
            if (inPinsSection && trimmed.endsWith(':')) {
                pinSection = trimmed.slice(0, -1);
                continue;
            }
        }

        // indent=8: pin data entries
        if (indent === 8 && currentBoard && pinSection) {
            if (trimmed.startsWith('- "')) {
                const val = trimmed.slice(3, -1);
                if (['gpio', 'pwm', 'adc'].includes(pinSection)) {
                    result.boards[currentBoard].pins[pinSection].push(val);
                }
            } else if (trimmed.includes(': {')) {
                // e.g. hspi: { sck: "GPIO14", mosi: "GPIO13", miso: "GPIO12", cs: "GPIO15" }
                const colonBrace = trimmed.indexOf(': {');
                const key = trimmed.slice(0, colonBrace).trim();
                const inner = trimmed.slice(colonBrace + 3).replace(/}$/, '').trim();
                const obj = {};
                for (const pair of inner.split(',')) {
                    const ci = pair.indexOf(':');
                    if (ci < 0) continue;
                    const k = pair.slice(0, ci).trim();
                    let v = pair.slice(ci + 1).trim();
                    if (v.startsWith('"') && v.endsWith('"')) v = v.slice(1, -1);
                    obj[k] = v;
                }
                result.boards[currentBoard].pins[pinSection][key] = obj;
            }
        }
    }
    return result;
}

function generateHeader(boardKey, boardData) {
    let out = `/*
 * hyp_board_config.h
 *
 * Generated by HyprAccel CORE-T3 codegen.
 * Target Board: ${boardData.name || boardKey}
 * Architecture: ${boardData.architecture || 'Unknown'}
 *
 * FW-P1..P6: Per-signal pin macros are emitted for every bus resource defined
 * in boards.yaml so that hyp_esp32_hw.cpp can initialise peripherals from this
 * generated configuration rather than hardcoded constants.
 */

#ifndef HYP_BOARD_CONFIG_H
#define HYP_BOARD_CONFIG_H

#define HYP_BOARD_NAME "${boardData.name || boardKey}"
#define HYP_BOARD_ARCH_${(boardData.architecture || '').toUpperCase().replace(/-/g, '_')} 1
#define HYP_SYSCLK_MHZ ${boardData.clock_freq_mhz || 0}

/* Transport Configuration */
`;

    if (boardData.fpga_transport) {
        out += `#define HYP_FPGA_TRANSPORT_TYPE "${boardData.fpga_transport}"\n`;
        out += `#define HYP_FPGA_TRANSPORT_${boardData.fpga_transport.toUpperCase()} 1\n`;
    }

    out += `\n/* Hardware Accelerators */\n`;
    if (boardData.accelerators && boardData.accelerators.length > 0) {
        for (const acc of boardData.accelerators) {
            out += `#define HYP_HAS_HW_${acc.toUpperCase()} 1\n`;
        }
    } else {
        out += `/* No hardware accelerators defined */\n`;
    }

    const pins = boardData.pins || {};

    // -------------------------------------------------------------------------
    // SPI bus pin macros (FW-P3)
    // -------------------------------------------------------------------------
    const spiEntries = Object.entries(pins.spi || {});
    if (spiEntries.length > 0) {
        out += `\n/* SPI Bus Pin Definitions (FW-P3) */\n`;
        for (const [inst, signals] of spiEntries) {
            const macro = `HYP_RESOURCE_SPI_${inst.toUpperCase()}`;
            out += `#define ${macro} 1\n`;
            for (const [role, pin] of Object.entries(signals)) {
                const num = gpioNum(pin);
                if (num >= 0) {
                    out += `#define ${macro}_${role.toUpperCase()}_PIN ${num}  /* ${pin} */\n`;
                } else {
                    out += `#define ${macro}_${role.toUpperCase()}_PIN_STR "${pin}"\n`;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // I2C bus pin macros (FW-P4)
    // -------------------------------------------------------------------------
    const i2cEntries = Object.entries(pins.i2c || {});
    if (i2cEntries.length > 0) {
        out += `\n/* I2C Bus Pin Definitions (FW-P4) */\n`;
        for (const [inst, signals] of i2cEntries) {
            const macro = `HYP_RESOURCE_I2C_${inst.toUpperCase()}`;
            out += `#define ${macro} 1\n`;
            for (const [role, pin] of Object.entries(signals)) {
                const num = gpioNum(pin);
                if (num >= 0) {
                    out += `#define ${macro}_${role.toUpperCase()}_PIN ${num}  /* ${pin} */\n`;
                } else {
                    out += `#define ${macro}_${role.toUpperCase()}_PIN_STR "${pin}"\n`;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // UART pin macros (FW-P2)
    // -------------------------------------------------------------------------
    const uartEntries = Object.entries(pins.uart || {});
    if (uartEntries.length > 0) {
        out += `\n/* UART Pin Definitions (FW-P2) */\n`;
        for (const [inst, signals] of uartEntries) {
            const macro = `HYP_RESOURCE_UART_${inst.toUpperCase()}`;
            out += `#define ${macro} 1\n`;
            for (const [role, pin] of Object.entries(signals)) {
                const num = gpioNum(pin);
                if (num >= 0) {
                    out += `#define ${macro}_${role.toUpperCase()}_PIN ${num}  /* ${pin} */\n`;
                } else {
                    out += `#define ${macro}_${role.toUpperCase()}_PIN_STR "${pin}"\n`;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // PWM pin macros (FW-P5)
    // -------------------------------------------------------------------------
    if ((pins.pwm || []).length > 0) {
        out += `\n/* PWM Pin Definitions (FW-P5) */\n`;
        for (const pin of pins.pwm) {
            const macro = `HYP_RESOURCE_PWM_${pin.toUpperCase()}`;
            const num = gpioNum(pin);
            if (num >= 0) {
                out += `#define ${macro} 1\n`;
                out += `#define ${macro}_PIN ${num}  /* ${pin} */\n`;
            }
        }
    }

    // -------------------------------------------------------------------------
    // ADC pin macros (FW-P6)
    // -------------------------------------------------------------------------
    if ((pins.adc || []).length > 0) {
        out += `\n/* ADC Pin Definitions (FW-P6) */\n`;
        for (const pin of pins.adc) {
            const macro = `HYP_RESOURCE_ADC_${pin.toUpperCase()}`;
            const num = gpioNum(pin);
            if (num >= 0) {
                out += `#define ${macro} 1\n`;
                out += `#define ${macro}_PIN ${num}  /* ${pin} */\n`;
            }
        }
    }

    // -------------------------------------------------------------------------
    // GPIO capability macros (FW-P1)
    // -------------------------------------------------------------------------
    if ((pins.gpio || []).length > 0) {
        out += `\n/* GPIO Pin Definitions (FW-P1) */\n`;
        for (const pin of pins.gpio) {
            const macro = `HYP_RESOURCE_GPIO_${pin.toUpperCase()}`;
            const num = gpioNum(pin);
            if (num >= 0) {
                out += `#define ${macro} 1\n`;
                out += `#define ${macro}_PIN ${num}  /* ${pin} */\n`;
            }
        }
    }

    out += `\n/*\n * Project Pin Assignments\n * (Augmented by MBD-T1b during node graph synthesis with\n * HYP_PIN_* and HYP_PERIPH_* defines for configured hardware resources.)\n */\n`;
    out += `\n#endif /* HYP_BOARD_CONFIG_H */\n`;
    return out;
}

function main() {
    const args = process.argv.slice(2);
    if (args.length < 3) {
        console.error("Usage: node gen_board_config.js <target_board> <boards.yaml> <out_dir>");
        process.exit(1);
    }

    const targetBoard = args[0];
    const yamlPath = args[1];
    const outDir = args[2];

    if (!fs.existsSync(yamlPath)) {
        console.error(`Error: File ${yamlPath} not found.`);
        process.exit(1);
    }

    const yamlContent = fs.readFileSync(yamlPath, 'utf8');
    const parsed = parseSimpleYaml(yamlContent);

    if (!parsed.boards[targetBoard]) {
        console.error(`Error: Board '${targetBoard}' not found in ${yamlPath}.`);
        console.error(`Available boards: ${Object.keys(parsed.boards).join(', ')}`);
        process.exit(1);
    }

    const headerContent = generateHeader(targetBoard, parsed.boards[targetBoard]);

    if (!fs.existsSync(outDir)) {
        fs.mkdirSync(outDir, { recursive: true });
    }

    const outPath = path.join(outDir, 'hyp_board_config.h');
    fs.writeFileSync(outPath, headerContent, 'utf8');

    console.log(`Successfully generated ${outPath} for ${targetBoard}`);
}

main();
