'use strict';
/*
 * Plain Node + assert tests for pin_conflicts.js. No framework.
 * Run: node boards/codegen/test/pin_conflicts.test.js
 */

const assert = require('assert');
const fs     = require('fs');
const path   = require('path');
const yaml   = require('../node_modules/js-yaml');

const { checkPinConflicts } = require('../pin_conflicts.js');

const BOARDS_YAML_PATH = path.join(__dirname, '..', '..', 'boards.yaml');
const boardsDoc = yaml.load(fs.readFileSync(BOARDS_YAML_PATH, 'utf8'));
const esp32 = boardsDoc.boards.esp32;
const thejas32 = boardsDoc.boards.thejas32;

const REPO_ROOT = path.join(__dirname, '..', '..', '..');

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

function codesOf(issues) {
    return issues.map(i => i.code);
}

function hw(assignments) {
    return { version: 1, board: 'esp32', assignments, resources: {} };
}

/* ---------------------------------------------------------------------- */
/* Real-world sample: demo_project must produce no errors and no warnings */
/* ---------------------------------------------------------------------- */

test('demo_project hardware + its graphs produce no errors or warnings', () => {
    const demoDir = path.join(REPO_ROOT, '.hypraccel', 'projects', 'demo_project');
    const hardware = JSON.parse(fs.readFileSync(path.join(demoDir, 'hardware', 'hardware.json'), 'utf8'));
    const board = boardsDoc.boards[hardware.board];
    assert.ok(board, `board '${hardware.board}' exists in boards.yaml`);

    const graphsDir = path.join(demoDir, 'graphs');
    const graphs = fs.readdirSync(graphsDir)
        .filter(f => f.endsWith('.json'))
        .map(f => JSON.parse(fs.readFileSync(path.join(graphsDir, f), 'utf8')));

    const { errors, warnings } = checkPinConflicts(hardware, board, graphs);
    assert.deepStrictEqual(errors, [], `expected no errors, got: ${JSON.stringify(errors, null, 2)}`);
    assert.deepStrictEqual(warnings, [], `expected no warnings, got: ${JSON.stringify(warnings, null, 2)}`);
});

/* ---------------------------------------------------------------------- */
/* UART completeness                                                      */
/* ---------------------------------------------------------------------- */

test('complete UART0 (tx + rx) produces nothing', () => {
    const hardware = hw([
        { node: '', role: 'tx', pin: 'GPIO1', resource: 'uart.uart0' },
        { node: '', role: 'rx', pin: 'GPIO3', resource: 'uart.uart0' }
    ]);
    const { errors, warnings } = checkPinConflicts(hardware, esp32, []);
    assert.deepStrictEqual(errors, []);
    assert.deepStrictEqual(warnings, []);
});

test('UART0 TX only produces a BUS_PARTIAL warning', () => {
    const hardware = hw([
        { node: '', role: 'tx', pin: 'GPIO1', resource: 'uart.uart0' }
    ]);
    const { errors, warnings } = checkPinConflicts(hardware, esp32, []);
    assert.deepStrictEqual(errors, []);
    assert.strictEqual(warnings.length, 1);
    assert.strictEqual(warnings[0].code, 'BUS_PARTIAL');
    assert.strictEqual(warnings[0].resource, 'uart.uart0');
    assert.match(warnings[0].message, /TX assigned without RX/);
});

/* ---------------------------------------------------------------------- */
/* I2C / SPI completeness                                                  */
/* ---------------------------------------------------------------------- */

test('I2C with only SDA produces a BUS_INCOMPLETE error', () => {
    const hardware = hw([
        { node: '', role: 'sda', pin: 'GPIO21', resource: 'i2c.i2c0' }
    ]);
    const { errors } = checkPinConflicts(hardware, esp32, []);
    assert.strictEqual(errors.length, 1);
    assert.strictEqual(errors[0].code, 'BUS_INCOMPLETE');
    assert.strictEqual(errors[0].resource, 'i2c.i2c0');
    assert.match(errors[0].message, /SCL/);
});

test('SPI without CS produces a BUS_PARTIAL warning', () => {
    const hardware = hw([
        { node: '', role: 'sck', pin: 'GPIO14', resource: 'spi.hspi' },
        { node: '', role: 'mosi', pin: 'GPIO13', resource: 'spi.hspi' },
        { node: '', role: 'miso', pin: 'GPIO12', resource: 'spi.hspi' }
    ]);
    const { errors, warnings } = checkPinConflicts(hardware, esp32, []);
    assert.deepStrictEqual(errors, []);
    assert.strictEqual(warnings.length, 1);
    assert.strictEqual(warnings[0].code, 'BUS_PARTIAL');
    assert.match(warnings[0].message, /CS/);
});

/* ---------------------------------------------------------------------- */
/* PIN_MULTIPLE_FUNCTIONS                                                  */
/* ---------------------------------------------------------------------- */

test('GPIO1 as uart.uart0 tx AND gpio.GPIO1 -> PIN_MULTIPLE_FUNCTIONS', () => {
    const hardware = hw([
        { node: '', role: 'tx', pin: 'GPIO1', resource: 'uart.uart0' },
        { node: '', role: 'gpio', pin: 'GPIO1', resource: 'gpio.GPIO1' }
    ]);
    const { errors } = checkPinConflicts(hardware, esp32, []);
    const multi = errors.filter(e => e.code === 'PIN_MULTIPLE_FUNCTIONS');
    assert.strictEqual(multi.length, 1);
    assert.strictEqual(multi[0].pin, 'GPIO1');
});

/* ---------------------------------------------------------------------- */
/* GRAPH_PIN_CONFLICT                                                      */
/* ---------------------------------------------------------------------- */

test('graph GPIOInput on gpio.GPIO1 while GPIO1 is uart.uart0 tx -> GRAPH_PIN_CONFLICT with node id', () => {
    const hardware = hw([
        { node: '', role: 'tx', pin: 'GPIO1', resource: 'uart.uart0' }
    ]);
    const graph = {
        nodes: [
            { id: 'btn', type: 'GPIOInput', params: { hardwareResource: 'gpio.GPIO1' } }
        ]
    };
    const { errors } = checkPinConflicts(hardware, esp32, [graph]);
    const conflicts = errors.filter(e => e.code === 'GRAPH_PIN_CONFLICT');
    assert.strictEqual(conflicts.length, 1);
    assert.strictEqual(conflicts[0].node, 'btn');
    assert.strictEqual(conflicts[0].pin, 'GPIO1');
    assert.match(conflicts[0].message, /'btn'/);
});

test('two graph nodes on gpio.GPIO25 and pwm.GPIO25 -> GRAPH_PIN_CONFLICT', () => {
    const hardware = hw([]);
    const graph = {
        nodes: [
            { id: 'n1', type: 'GPIOInput', params: { hardwareResource: 'gpio.GPIO25' } },
            { id: 'n2', type: 'PWMOutput', params: { hardwareResource: 'pwm.GPIO25' } }
        ]
    };
    const { errors } = checkPinConflicts(hardware, esp32, [graph]);
    const conflicts = errors.filter(e => e.code === 'GRAPH_PIN_CONFLICT');
    assert.strictEqual(conflicts.length, 1);
    assert.strictEqual(conflicts[0].pin, 'GPIO25');
});

test('PWM output on pwm.GPIO25 with GPIO25 assigned pwm -> fine', () => {
    const hardware = hw([
        { node: '', role: 'output', pin: 'GPIO25', resource: 'pwm.GPIO25' }
    ]);
    const graph = {
        nodes: [
            { id: 'led', type: 'PWMOutput', params: { hardwareResource: 'pwm.GPIO25' } }
        ]
    };
    const { errors, warnings } = checkPinConflicts(hardware, esp32, [graph]);
    assert.deepStrictEqual(errors, []);
    assert.deepStrictEqual(warnings, []);
});

/* ---------------------------------------------------------------------- */
/* GRAPH_RESOURCE_UNASSIGNED                                               */
/* ---------------------------------------------------------------------- */

test('graph referencing unassigned bus resource -> GRAPH_RESOURCE_UNASSIGNED warning', () => {
    const hardware = hw([]);
    const graph = {
        nodes: [
            { id: 'sensor', type: 'SensorInput', params: { hardwareResource: 'i2c.i2c0' } }
        ]
    };
    const { errors, warnings } = checkPinConflicts(hardware, esp32, [graph]);
    assert.deepStrictEqual(errors, []);
    assert.strictEqual(warnings.length, 1);
    assert.strictEqual(warnings[0].code, 'GRAPH_RESOURCE_UNASSIGNED');
    assert.strictEqual(warnings[0].node, 'sensor');
});

/* ---------------------------------------------------------------------- */
/* INVALID_ASSIGNMENT                                                      */
/* ---------------------------------------------------------------------- */

test('invalid pin for uart.uart0 tx -> INVALID_ASSIGNMENT', () => {
    const hardware = hw([
        { node: '', role: 'tx', pin: 'GPIO99', resource: 'uart.uart0' }
    ]);
    const { errors } = checkPinConflicts(hardware, esp32, []);
    assert.strictEqual(errors.length, 1);
    assert.strictEqual(errors[0].code, 'INVALID_ASSIGNMENT');
    assert.strictEqual(errors[0].resource, 'uart.uart0');
});

test('unknown resource id -> INVALID_ASSIGNMENT', () => {
    const hardware = hw([
        { node: '', role: 'tx', pin: 'GPIO1', resource: 'uart.uart9' }
    ]);
    const { errors } = checkPinConflicts(hardware, esp32, []);
    assert.strictEqual(errors.length, 1);
    assert.strictEqual(errors[0].code, 'INVALID_ASSIGNMENT');
});

test('role not valid for resource type -> INVALID_ASSIGNMENT', () => {
    const hardware = hw([
        { node: '', role: 'sck', pin: 'GPIO1', resource: 'uart.uart0' }
    ]);
    const { errors } = checkPinConflicts(hardware, esp32, []);
    assert.strictEqual(errors.length, 1);
    assert.strictEqual(errors[0].code, 'INVALID_ASSIGNMENT');
});

test('gpio/pwm/adc resource whose instance != pin -> INVALID_ASSIGNMENT', () => {
    const hardware = hw([
        { node: '', role: 'gpio', pin: 'GPIO2', resource: 'gpio.GPIO1' }
    ]);
    const { errors } = checkPinConflicts(hardware, esp32, []);
    assert.strictEqual(errors.length, 1);
    assert.strictEqual(errors[0].code, 'INVALID_ASSIGNMENT');
});

/* ---------------------------------------------------------------------- */
/* ESP32 i2c0 `devices` map                                                */
/* ---------------------------------------------------------------------- */

test('esp32 i2c0 devices map is never treated as a role', () => {
    // "devices" must not be usable as a role, and must not silently satisfy
    // BUS_INCOMPLETE (sda/scl are still required).
    const hardware = hw([
        { node: '', role: 'devices', pin: 'GPIO21', resource: 'i2c.i2c0' }
    ]);
    const { errors } = checkPinConflicts(hardware, esp32, []);
    assert.strictEqual(errors.length, 1);
    assert.strictEqual(errors[0].code, 'INVALID_ASSIGNMENT');
});

/* ---------------------------------------------------------------------- */
/* thejas32 sample                                                         */
/* ---------------------------------------------------------------------- */

test('thejas32 sample with complete SPI0 is clean', () => {
    const hardware = {
        version: 1,
        board: 'thejas32',
        assignments: [
            { node: '', role: 'sck', pin: 'SPI0CLK', resource: 'spi.spi0' },
            { node: '', role: 'mosi', pin: 'SPI0MOSI', resource: 'spi.spi0' },
            { node: '', role: 'miso', pin: 'SPI0MISO', resource: 'spi.spi0' },
            { node: '', role: 'cs', pin: 'SPI0CSN', resource: 'spi.spi0' }
        ],
        resources: {}
    };
    const { errors, warnings } = checkPinConflicts(hardware, thejas32, []);
    assert.deepStrictEqual(errors, []);
    assert.deepStrictEqual(warnings, []);
});

/* ---------------------------------------------------------------------- */
/* Empty / missing inputs                                                  */
/* ---------------------------------------------------------------------- */

test('empty assignments and missing graphs produce a clean result', () => {
    const hardware = { version: 1, board: 'esp32' }; // no assignments key at all
    const { errors, warnings } = checkPinConflicts(hardware, esp32);
    assert.deepStrictEqual(errors, []);
    assert.deepStrictEqual(warnings, []);
});

test('graphs with no nodes / missing nodes array produce a clean result', () => {
    const hardware = hw([]);
    const { errors, warnings } = checkPinConflicts(hardware, esp32, [{}, { nodes: [] }]);
    assert.deepStrictEqual(errors, []);
    assert.deepStrictEqual(warnings, []);
});

test('accelerator resources never conflict', () => {
    const hardware = hw([
        { node: '', role: 'n/a', pin: 'n/a', resource: 'accelerator.cordic' }
    ]);
    const graph = {
        nodes: [
            { id: 'cordic1', type: 'CordicOp', params: { hardwareResource: 'accelerator.cordic' } }
        ]
    };
    const { errors, warnings } = checkPinConflicts(hardware, esp32, [graph]);
    assert.deepStrictEqual(errors, []);
    assert.deepStrictEqual(warnings, []);
});

/* ---------------------------------------------------------------------- */

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
