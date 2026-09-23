'use strict';
/*
 * Tests for boards/codegen/project_defines.js.
 * Plain Node + assert. Run: node boards/codegen/test/project_defines.test.js
 */

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');
const yaml = require('js-yaml');

const REPO = path.resolve(__dirname, '../../..');
const FIXTURES = path.join(REPO, 'mbd/editor/test/fixtures/codegen_baseline');
const { renderProjectDefines, injectProjectDefines } = require('../project_defines.js');

const boardsYaml = yaml.load(fs.readFileSync(path.join(REPO, 'boards/boards.yaml'), 'utf8'));
const esp32Board = boardsYaml.boards.esp32;
const thejas32Board = boardsYaml.boards.thejas32;

let failures = 0;
function test(name, fn) {
    try {
        fn();
        console.log(`ok - ${name}`);
    } catch (err) {
        failures++;
        console.error(`FAIL - ${name}`);
        console.error(err && err.stack ? err.stack : err);
    }
}

function readFixture(name) {
    return fs.readFileSync(path.join(FIXTURES, name), 'utf8');
}

/* -------------------------------------------------------------------------
 * Fixture hardware objects
 * ---------------------------------------------------------------------- */

// demo_project: verbatim copy of the checked-in project hardware.json — a
// pure web-editor config (every assignment has a non-empty node).
const demoHardware = JSON.parse(
    fs.readFileSync(path.join(REPO, '.hypraccel/projects/demo_project/hardware/hardware.json'), 'utf8')
);

// web_uart: reconstructed canonical hardware.json for the project capture.js
// creates via POST /api/projects with hardware = { board: 'esp32', assignments: [...],
// configurations: { 'uart.uart2': { baudRate: 57600 } } }. The server's
// projectHardware() canonicalises this into a full esp32 resources map (one
// entry per boards.yaml pin/bus, same shape as demo_project's) with the
// posted `configurations` folded into the matching resource's `configuration`
// field. We build that canonical form by cloning demo_project's resource map
// (same board, so the same resource set) and clearing project-specific state.
function buildWebUartHardware() {
    const resources = JSON.parse(JSON.stringify(demoHardware.resources));
    resources['uart.uart2'].configuration = { baudRate: 57600 };
    return {
        version: 1,
        board: 'esp32',
        resources,
        assignments: [
            { node: 'SerialLink[0]', role: 'tx', pin: 'GPIO17', resource: 'uart.uart2' },
            { node: 'SerialLink[0]', role: 'rx', pin: 'GPIO16', resource: 'uart.uart2' },
            { node: 'Servo[0]', role: 'output', pin: 'GPIO25', resource: 'pwm.GPIO25' },
        ],
        devices: [],
    };
}
const webUartHardware = buildWebUartHardware();

function legacyConfig(hardware) {
    return Object.assign({}, hardware, { clock: null, configWarnings: [] });
}

/* -------------------------------------------------------------------------
 * 1. Regression: byte-identical to golden fixtures for web-editor-only
 *    configs (clock null, every assignment has a non-empty node).
 * ---------------------------------------------------------------------- */

test('regression: materialize demo_project byte-identical', () => {
    const cli = readFixture('cli_esp32.h');
    const golden = readFixture('materialize_demo_project.h');
    const got = injectProjectDefines(cli, legacyConfig(demoHardware), esp32Board, { variant: 'materialize' });
    assert.strictEqual(got, golden);
});

test('regression: api demo_project byte-identical', () => {
    const cli = readFixture('cli_esp32.h');
    const golden = readFixture('api_generate_demo_project.h');
    const got = injectProjectDefines(cli, legacyConfig(demoHardware), esp32Board, { variant: 'api' });
    assert.strictEqual(got, golden);
});

test('regression: materialize web_uart byte-identical', () => {
    const cli = readFixture('cli_esp32.h');
    const golden = readFixture('materialize_web_uart.h');
    const got = injectProjectDefines(cli, legacyConfig(webUartHardware), esp32Board, { variant: 'materialize' });
    assert.strictEqual(got, golden);
});

test('regression: api web_uart byte-identical', () => {
    const cli = readFixture('cli_esp32.h');
    const golden = readFixture('api_generate_web_uart.h');
    const got = injectProjectDefines(cli, legacyConfig(webUartHardware), esp32Board, { variant: 'api' });
    assert.strictEqual(got, golden);
});

test('regression: web-only project with warnings appends only the warnings block', () => {
    const cli = readFixture('cli_esp32.h');
    const golden = readFixture('materialize_demo_project.h');
    const got = injectProjectDefines(cli, legacyConfig(demoHardware), esp32Board, {
        variant: 'materialize',
        warnings: ['GPIO4 shared by two resources'],
    });
    assert.notStrictEqual(got, golden);
    // Everything up to the marker in `golden` must still appear verbatim,
    // immediately followed by the warnings block, and no pin-function/clock
    // blocks must have been introduced.
    const marker = '#endif /* HYP_BOARD_CONFIG_H */';
    const goldenBody = golden.slice(0, golden.indexOf(marker));
    assert.ok(got.startsWith(goldenBody.replace(/\n\n$/, '\n\n')) || got.includes(goldenBody.trim()));
    assert.ok(got.includes('/* Configuration warnings: */'));
    assert.ok(got.includes('/* GPIO4 shared by two resources */'));
    assert.ok(!got.includes('HYP_PINFUNC_'));
    assert.ok(!got.includes('HYP_CLOCK_CONFIG_PRESENT'));
});

/* -------------------------------------------------------------------------
 * 2. Desktop-only esp32 config
 * ---------------------------------------------------------------------- */

function desktopEsp32Config() {
    return {
        version: 1,
        board: 'esp32',
        resources: {},
        assignments: [
            { node: '', role: 'tx', pin: 'GPIO1', resource: 'uart.uart0' },
            { node: '', role: 'rx', pin: 'GPIO3', resource: 'uart.uart0' },
            { node: '', role: 'output', pin: 'GPIO25', resource: 'pwm.GPIO25' },
        ],
        devices: [],
        clock: {
            board: 'esp32',
            selections: { bbpll: '8', cpu_mux: 'bbpll', uart_clk_sel: 'apb_clk' },
            resolved: { cpu_div: 160, apb_clk: 80 },
            errors: [],
            bestEffort: true,
            unverifiedNodes: [],
        },
        configWarnings: [],
    };
}

test('desktop esp32: pin-function macros present, no HYP_PIN_ for empty nodes', () => {
    const rendered = renderProjectDefines(desktopEsp32Config(), esp32Board, { variant: 'materialize' });
    assert.ok(rendered.includes('#define HYP_PINFUNC_GPIO1 "uart.uart0.tx"'));
    assert.ok(rendered.includes('#define HYP_PINFUNC_GPIO1_UART_UART0_TX 1'));
    assert.ok(rendered.includes('#define HYP_PINFUNC_GPIO3 "uart.uart0.rx"'));
    assert.ok(rendered.includes('#define HYP_PINFUNC_GPIO25 "pwm"'));
    assert.ok(rendered.includes('#define HYP_PINFUNC_GPIO25_PWM 1'));
    assert.ok(!/HYP_PIN__/.test(rendered));
    assert.ok(!/HYP_PIN_[A-Z0-9_]*_(TX|RX|OUTPUT)\b/.test(rendered), 'no legacy HYP_PIN_ macro for empty-node assignments');
});

test('desktop esp32: clock macros + CPU alias present', () => {
    const rendered = renderProjectDefines(desktopEsp32Config(), esp32Board, { variant: 'materialize' });
    assert.ok(rendered.includes('#define HYP_CLOCK_CONFIG_PRESENT 1'));
    assert.ok(rendered.includes('#define HYP_CLOCK_BEST_EFFORT 1'));
    assert.ok(rendered.includes('#define HYP_CLOCK_CPU_DIV_MHZ 160'));
    assert.ok(rendered.includes('#define HYP_CLOCK_APB_CLK_MHZ 80'));
    assert.ok(rendered.includes('#define HYP_CPU_FREQ_MHZ 160'));
    assert.ok(rendered.includes('#define HYP_CPU_CLK_SRC_PLL 1'));
    assert.ok(!rendered.includes('HYP_CLOCK_NONSTANDARD_CPU_FREQ'));
});

test('desktop esp32: pin-function block is present for both variants identically triggered', () => {
    const cfg = desktopEsp32Config();
    const cli = readFixture('cli_esp32.h');
    const materialize = injectProjectDefines(cli, cfg, esp32Board, { variant: 'materialize' });
    const api = injectProjectDefines(cli, cfg, esp32Board, { variant: 'api' });
    assert.ok(materialize.includes('/* Pin functions'));
    assert.ok(api.includes('/* Pin functions'));
});

/* -------------------------------------------------------------------------
 * 3. thejas32: all clock values unverified, no ESP32 aliases
 * ---------------------------------------------------------------------- */

test('thejas32: all resolved clock values marked unverified, no ESP32 aliases', () => {
    const config = {
        version: 1,
        board: 'thejas32',
        resources: {},
        assignments: [{ node: '', role: 'gpio', pin: 'GPIO0', resource: 'gpio.GPIO0' }],
        devices: [],
        clock: {
            board: 'thejas32',
            selections: {},
            resolved: { osc: 100, cpu_clk: 100, periph_clk: 100 },
            errors: [],
            bestEffort: true,
            unverifiedNodes: ['osc', 'cpu_clk', 'periph_clk'],
        },
        configWarnings: [],
    };
    const rendered = renderProjectDefines(config, thejas32Board, { variant: 'materialize' });
    for (const id of ['OSC', 'CPU_CLK', 'PERIPH_CLK']) {
        const re = new RegExp(`#define HYP_CLOCK_${id}_MHZ 100  /\\* unverified \\*/`);
        assert.ok(re.test(rendered), `expected unverified ${id} MHZ macro`);
    }
    assert.ok(!rendered.includes('HYP_CPU_FREQ_MHZ'));
    assert.ok(!rendered.includes('HYP_CPU_CLK_SRC_'));
    assert.ok(!rendered.includes('HYP_APB_FREQ_MHZ'));
});

/* -------------------------------------------------------------------------
 * 4. Nonstandard CPU frequency flag
 * ---------------------------------------------------------------------- */

test('nonstandard CPU frequency sets HYP_CLOCK_NONSTANDARD_CPU_FREQ', () => {
    const config = desktopEsp32Config();
    config.clock.resolved.cpu_div = 123;
    const rendered = renderProjectDefines(config, esp32Board, { variant: 'materialize' });
    assert.ok(rendered.includes('#define HYP_CPU_FREQ_MHZ 123'));
    assert.ok(rendered.includes('#define HYP_CLOCK_NONSTANDARD_CPU_FREQ 1'));
});

test('standard CPU frequencies (80/160/240) do not set the nonstandard flag', () => {
    for (const freq of [80, 160, 240]) {
        const config = desktopEsp32Config();
        config.clock.resolved.cpu_div = freq;
        const rendered = renderProjectDefines(config, esp32Board, { variant: 'materialize' });
        assert.ok(!rendered.includes('HYP_CLOCK_NONSTANDARD_CPU_FREQ'), `freq ${freq} should not be flagged`);
    }
});

/* -------------------------------------------------------------------------
 * 5. Clock errors
 * ---------------------------------------------------------------------- */

test('clock.errors produce HYP_CLOCK_HAS_ERRORS and comment lines', () => {
    const config = desktopEsp32Config();
    config.clock.errors = ['cpu_div selection 5 exceeds max_mhz for bbpll x8'];
    const rendered = renderProjectDefines(config, esp32Board, { variant: 'materialize' });
    assert.ok(rendered.includes('#define HYP_CLOCK_HAS_ERRORS 1'));
    assert.ok(rendered.includes('/* cpu_div selection 5 exceeds max_mhz for bbpll x8 */'));
});

/* -------------------------------------------------------------------------
 * 6. Mixed project: some assignments named, some desktop-only
 * ---------------------------------------------------------------------- */

test('mixed project keeps legacy macros for named nodes and adds pin functions for all', () => {
    const config = {
        version: 1,
        board: 'esp32',
        resources: JSON.parse(JSON.stringify(demoHardware.resources)),
        assignments: [
            { node: 'SensorInput[1]', role: 'mosi', pin: 'GPIO13', resource: 'spi.hspi' },
            { node: '', role: 'output', pin: 'GPIO25', resource: 'pwm.GPIO25' },
        ],
        devices: [],
        clock: null,
        configWarnings: [],
    };
    const rendered = renderProjectDefines(config, esp32Board, { variant: 'materialize' });
    // Legacy macro for the named assignment is preserved.
    assert.ok(rendered.includes('#define HYP_PIN_SENSORINPUT_1_MOSI "GPIO13"  /* SensorInput[1] → spi.hspi.mosi */'));
    // No legacy macro is produced for the empty-node assignment.
    assert.ok(!rendered.includes('_OUTPUT "GPIO25"'));
    // Pin-function block covers both, since clock is null but an empty node
    // is present (desktop trigger).
    assert.ok(rendered.includes('#define HYP_PINFUNC_GPIO13 "spi.hspi.mosi"'));
    assert.ok(rendered.includes('#define HYP_PINFUNC_GPIO25 "pwm"'));
});

/* -------------------------------------------------------------------------
 * 7. Comment escaping
 * ---------------------------------------------------------------------- */

test('comment escaping: warnings/errors containing "*/" do not break the C comment', () => {
    const config = desktopEsp32Config();
    config.clock.errors = ['unexpected */ sequence in note'];
    const rendered = renderProjectDefines(config, esp32Board, {
        variant: 'materialize',
        warnings: ['pin GPIO4 /* shared */ across resources'],
    });
    assert.ok(!rendered.includes('*/ sequence in note'), 'raw "*/ " must be escaped');
    // The escaped form must not contain a bare comment terminator except at
    // intended comment boundaries — full proof that every comment body is
    // still parseable is the compile-check test below, which reuses this
    // exact kind of input.
    assert.ok(rendered.includes('sequence in note'));
    assert.ok(rendered.includes('shared'));
});

/* -------------------------------------------------------------------------
 * 8. Compile-check: generated desktop header has no macro redefinition or
 *    syntax errors.
 * ---------------------------------------------------------------------- */

test('compile-check: generated desktop header compiles with -Werror', () => {
    const config = desktopEsp32Config();
    config.clock.errors = ['unexpected */ sequence in a clock error'];
    const rendered = injectProjectDefines(readFixture('cli_esp32.h'), config, esp32Board, {
        variant: 'materialize',
        warnings: ['a warning with */ inside it'],
    });

    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-pd-test-'));
    try {
        const headerPath = path.join(tmpDir, 'hyp_board_config.h');
        const cPath = path.join(tmpDir, 'check.c');
        fs.writeFileSync(headerPath, rendered, 'utf8');
        fs.writeFileSync(cPath, '#include "hyp_board_config.h"\nint main(void) { return 0; }\n', 'utf8');

        let compiler = null;
        let compilerArgs = null;
        try {
            execFileSync('cc', ['--version'], { stdio: 'pipe' });
            compiler = 'cc';
        } catch (e) {
            try {
                execFileSync('nix-shell', [path.join(REPO, 'desktop/shell.nix'), '--run', 'cc --version'], { stdio: 'pipe' });
                compiler = 'nix-shell';
            } catch (e2) {
                compiler = null;
            }
        }

        if (!compiler) {
            console.log('skip - no C compiler available (tried cc on PATH and nix-shell desktop/shell.nix)');
            return;
        }

        if (compiler === 'cc') {
            execFileSync('cc', ['-fsyntax-only', '-Werror', '-x', 'c', cPath], { stdio: 'pipe' });
        } else {
            execFileSync('nix-shell', [path.join(REPO, 'desktop/shell.nix'), '--run', `cc -fsyntax-only -Werror -x c "${cPath}"`], {
                stdio: 'pipe',
            });
        }
    } finally {
        fs.rmSync(tmpDir, { recursive: true, force: true });
    }
});

test('hardware-coupled clock nodes follow the node they track (boards.yaml follows)', () => {
    const base = { board: 'esp32', resources: {}, assignments: [], devices: [], configWarnings: [] };
    const clock = (selections) => ({ board: 'esp32', selections, resolved: {}, errors: [], bestEffort: true, unverifiedNodes: [] });
    let out = renderProjectDefines({ ...base, clock: clock({ bbpll: '8', cpu_mux: 'bbpll', apb_pll: '6', apb_clk: 'cpu_div' }) },
        esp32Board, { variant: 'materialize' });
    assert.ok(out.includes('#define HYP_CLOCK_APB_PLL_FACTOR 4\n'), 'PLL 320 MHz path must use APB divisor 4, not the default/selection');
    assert.ok(out.includes('#define HYP_CLOCK_APB_CLK_SRC_APB_PLL 1'), 'a selection must not override a follower');
    out = renderProjectDefines({ ...base, clock: clock({ cpu_mux: 'xtal' }) }, esp32Board, { variant: 'materialize' });
    assert.ok(out.includes('#define HYP_CLOCK_APB_CLK_SRC_CPU_DIV 1'), 'APB tracks CPU_CLK on the XTAL path');
    assert.ok(out.includes('#define HYP_CLOCK_APB_PLL_FACTOR 6\n'));
    assert.ok(out.includes('#define HYP_CPU_CLK_SRC_XTAL 1'));
});

/* ------------------------------------------------------------------------- */

if (failures > 0) {
    console.error(`\n${failures} test(s) failed.`);
    process.exit(1);
} else {
    console.log('\nAll tests passed.');
}
