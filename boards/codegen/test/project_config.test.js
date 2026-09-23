'use strict';
/*
 * Plain Node + assert tests for project_config.js. No framework.
 * Run: node boards/codegen/test/project_config.test.js
 * All fixtures live under fs.mkdtempSync(os.tmpdir()) — never touches the
 * real repo .hypraccel/ directory.
 */

const assert = require('assert');
const fs     = require('fs');
const os     = require('os');
const path   = require('path');

const { readClockConfig, mergeProjectConfig, readProjectConfig } = require('../project_config.js');

const BOARDS_YAML_PATH = path.join(__dirname, '..', '..', 'boards.yaml');

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        passed++;
        console.log(`ok - ${name}`);
    } catch (err) {
        failed++;
        console.error(`not ok - ${name}`);
        console.error(err && err.stack ? err.stack : err);
    }
}

function mkTempProjectDir() {
    return fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-project-config-test-'));
}

function writeHardware(projectDir, hardware) {
    const dir = path.join(projectDir, 'hardware');
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'hardware.json'), JSON.stringify(hardware, null, 2), 'utf8');
}

function writeClock(projectDir, clock) {
    const dir = path.join(projectDir, 'hardware');
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'clock.json'), JSON.stringify(clock, null, 2), 'utf8');
}

function writeRawClock(projectDir, text) {
    const dir = path.join(projectDir, 'hardware');
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'clock.json'), text, 'utf8');
}

const baseEsp32Hardware = () => ({
    version: 1,
    board: 'esp32',
    resources: {
        uart: {
            uart0: {
                configuration: { baudRate: 115200 },
            },
        },
    },
    devices: [{ id: 'imu', type: 'i2c-device' }],
    assignments: [
        { node: 'SerialOut[0]', pin: 'GPIO1', role: 'tx', resource: 'uart.uart0' },
    ],
});

// -------------------------------------------------------------------------
// readClockConfig
// -------------------------------------------------------------------------

test('readClockConfig: no clock.json returns null, hardware deep-equal to input', () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, baseEsp32Hardware());

    const clock = readClockConfig(projectDir);
    assert.strictEqual(clock, null);

    const merged = readProjectConfig(projectDir, BOARDS_YAML_PATH);
    const expectedHardware = baseEsp32Hardware();
    for (const key of Object.keys(expectedHardware)) {
        assert.deepStrictEqual(merged[key], expectedHardware[key], `field '${key}' mismatch`);
    }
    assert.strictEqual(merged.clock, null);
    assert.deepStrictEqual(merged.configWarnings, []);
});

test('readClockConfig: valid esp32 clock.json merges with bestEffort and correct unverifiedNodes', () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, baseEsp32Hardware());
    writeClock(projectDir, {
        version: 1,
        board: 'esp32',
        selections: { cpu_mux: 'bbpll', cpu_div: '2' },
        resolved: { cpu_div: 240, apb_clk: 80 },
        errors: [],
    });

    const merged = readProjectConfig(projectDir, BOARDS_YAML_PATH);
    assert.ok(merged.clock, 'expected clock to be present');
    assert.strictEqual(merged.clock.board, 'esp32');
    assert.strictEqual(merged.clock.bestEffort, true);
    assert.deepStrictEqual(merged.clock.selections, { cpu_mux: 'bbpll', cpu_div: '2' });
    assert.deepStrictEqual(merged.clock.resolved, { cpu_div: 240, apb_clk: 80 });
    assert.deepStrictEqual(merged.clock.errors, []);
    // Per boards.yaml, only ref_tick's note on esp32 mentions "unverified".
    assert.deepStrictEqual(merged.clock.unverifiedNodes, ['ref_tick']);
    assert.deepStrictEqual(merged.configWarnings, []);
});

test('thejas32: all clock nodes are unverified', () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, {
        version: 1,
        board: 'thejas32',
        resources: {},
        devices: [],
        assignments: [],
    });
    writeClock(projectDir, {
        version: 1,
        board: 'thejas32',
        selections: {},
        resolved: { osc: 100 },
        errors: [],
    });

    const merged = readProjectConfig(projectDir, BOARDS_YAML_PATH);
    assert.ok(merged.clock);
    assert.deepStrictEqual(merged.clock.unverifiedNodes.sort(), ['cpu_clk', 'osc', 'periph_clk'].sort());
});

test('board mismatch between clock.json and hardware.json => clock null + warning', () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, baseEsp32Hardware());
    writeClock(projectDir, {
        version: 1,
        board: 'thejas32',
        selections: {},
        resolved: {},
        errors: [],
    });

    const merged = readProjectConfig(projectDir, BOARDS_YAML_PATH);
    assert.strictEqual(merged.clock, null);
    assert.strictEqual(merged.configWarnings.length, 1);
    assert.match(merged.configWarnings[0], /board/i);
});

test('unknown node ids in selections/resolved are dropped with a warning each', () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, baseEsp32Hardware());
    writeClock(projectDir, {
        version: 1,
        board: 'esp32',
        selections: { cpu_mux: 'bbpll', bogus_node: 'x' },
        resolved: { cpu_div: 240, another_bogus: 5 },
        errors: [],
    });

    const merged = readProjectConfig(projectDir, BOARDS_YAML_PATH);
    assert.ok(merged.clock);
    assert.deepStrictEqual(merged.clock.selections, { cpu_mux: 'bbpll' });
    assert.deepStrictEqual(merged.clock.resolved, { cpu_div: 240 });
    assert.strictEqual(merged.configWarnings.length, 2);
    assert.ok(merged.configWarnings.some(w => w.includes('bogus_node')));
    assert.ok(merged.configWarnings.some(w => w.includes('another_bogus')));
});

test('non-finite / negative resolved values are dropped with a warning', () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, baseEsp32Hardware());
    const boardsDoc = require('js-yaml').load(fs.readFileSync(BOARDS_YAML_PATH, 'utf8'));
    const boardData = boardsDoc.boards.esp32;
    const clock = {
        board: 'esp32',
        selections: {},
        resolved: { cpu_div: NaN, apb_clk: -5, ref_tick: Infinity, i2c_clk: 40 },
        errors: [],
    };
    const merged = mergeProjectConfig(baseEsp32Hardware(), clock, boardData);
    assert.deepStrictEqual(merged.clock.resolved, { i2c_clk: 40 });
    // 3 dropped resolved values => 3 warnings
    assert.strictEqual(merged.configWarnings.length, 3);
});

test('missing clocks section on boardData => clock null + warning', () => {
    const projectDir = mkTempProjectDir();
    const hardware = baseEsp32Hardware();
    const boardDataNoClocks = { name: 'Fake board' }; // no clocks key
    const clock = { board: 'esp32', selections: {}, resolved: {}, errors: [] };
    const merged = mergeProjectConfig(hardware, clock, boardDataNoClocks);
    assert.strictEqual(merged.clock, null);
    assert.strictEqual(merged.configWarnings.length, 1);
    assert.match(merged.configWarnings[0], /clocks/i);
});

// -------------------------------------------------------------------------
// purity / mutation
// -------------------------------------------------------------------------

test('mergeProjectConfig does not mutate its inputs', () => {
    const hardware = baseEsp32Hardware();
    const clock = {
        board: 'esp32',
        selections: { cpu_mux: 'bbpll' },
        resolved: { cpu_div: 240 },
        errors: ['some computeClocks error'],
    };
    const boardsDoc = require('js-yaml').load(fs.readFileSync(BOARDS_YAML_PATH, 'utf8'));
    const boardData = boardsDoc.boards.esp32;

    const hardwareSnapshot = structuredClone(hardware);
    const clockSnapshot = structuredClone(clock);
    const boardDataSnapshot = structuredClone(boardData);

    mergeProjectConfig(hardware, clock, boardData);

    assert.deepStrictEqual(hardware, hardwareSnapshot);
    assert.deepStrictEqual(clock, clockSnapshot);
    assert.deepStrictEqual(boardData, boardDataSnapshot);
});

// -------------------------------------------------------------------------
// web-editor fields preserved
// -------------------------------------------------------------------------

test('web-editor fields (resources.*.configuration, devices, assignments[].node) preserved verbatim', () => {
    const projectDir = mkTempProjectDir();
    const hardware = baseEsp32Hardware();
    writeHardware(projectDir, hardware);

    const merged = readProjectConfig(projectDir, BOARDS_YAML_PATH);
    assert.deepStrictEqual(merged.resources.uart.uart0.configuration, { baudRate: 115200 });
    assert.deepStrictEqual(merged.devices, [{ id: 'imu', type: 'i2c-device' }]);
    assert.strictEqual(merged.assignments[0].node, 'SerialOut[0]');
});

// -------------------------------------------------------------------------
// error cases
// -------------------------------------------------------------------------

test('invalid JSON in clock.json throws with the file path in the message', () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, baseEsp32Hardware());
    writeRawClock(projectDir, '{ not valid json ');

    const clockPath = path.join(projectDir, 'hardware', 'clock.json');
    assert.throws(
        () => readProjectConfig(projectDir, BOARDS_YAML_PATH),
        (err) => err instanceof Error && err.message.includes(clockPath)
    );
});

test('clock.json missing version:1 throws with the file path', () => {
    const projectDir = mkTempProjectDir();
    writeClock(projectDir, { board: 'esp32', selections: {} });
    const clockPath = path.join(projectDir, 'hardware', 'clock.json');
    assert.throws(
        () => readClockConfig(projectDir),
        (err) => err instanceof Error && err.message.includes(clockPath)
    );
});

test('clock.json with non-object selections throws with the file path', () => {
    const projectDir = mkTempProjectDir();
    writeClock(projectDir, { version: 1, board: 'esp32', selections: 'nope' });
    const clockPath = path.join(projectDir, 'hardware', 'clock.json');
    assert.throws(
        () => readClockConfig(projectDir),
        (err) => err instanceof Error && err.message.includes(clockPath)
    );
});

test('older clock.json without resolved/errors defaults to {} / []', () => {
    const projectDir = mkTempProjectDir();
    writeClock(projectDir, { version: 1, board: 'esp32', selections: { cpu_mux: 'bbpll' } });
    const clock = readClockConfig(projectDir);
    assert.deepStrictEqual(clock.resolved, {});
    assert.deepStrictEqual(clock.errors, []);
});

test('missing hardware.json throws a clear error', () => {
    const projectDir = mkTempProjectDir();
    assert.throws(
        () => readProjectConfig(projectDir, BOARDS_YAML_PATH),
        (err) => err instanceof Error && /hardware\.json|hardware configuration/i.test(err.message)
    );
});

test("board 'toString' in hardware.json throws (own-property lookup, no prototype pollution)", () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, {
        version: 1,
        board: 'toString',
        resources: {},
        devices: [],
        assignments: [],
    });
    assert.throws(
        () => readProjectConfig(projectDir, BOARDS_YAML_PATH),
        (err) => err instanceof Error && /unknown board/i.test(err.message)
    );
});

test('unknown board in hardware.json throws', () => {
    const projectDir = mkTempProjectDir();
    writeHardware(projectDir, {
        version: 1,
        board: 'not_a_real_board',
        resources: {},
        devices: [],
        assignments: [],
    });
    assert.throws(
        () => readProjectConfig(projectDir, BOARDS_YAML_PATH),
        (err) => err instanceof Error && /unknown board/i.test(err.message)
    );
});

// -------------------------------------------------------------------------

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
