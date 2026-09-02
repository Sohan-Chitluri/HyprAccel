'use strict';

const assert = require('assert');
const { generate } = require('../graph_to_c');

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
function makeGraph(id, nodes, edges = []) {
    return { format: 'hypraccel.mbd.graph', version: 1, id, nodes, edges };
}

function constNode(id, value) {
    return { id, type: 'Constant', params: { value } };
}

function binaryGraph(nodeType, aVal, bVal) {
    return makeGraph('g',
        [constNode('a', aVal), constNode('b', bVal), { id: 'out', type: nodeType, params: {} }],
        [
            { from: { node: 'a', port: 'value' }, to: { node: 'out', port: 'a' } },
            { from: { node: 'b', port: 'value' }, to: { node: 'out', port: 'b' } }
        ]
    );
}

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        console.log(`[PASS] ${name}`);
        passed++;
    } catch (err) {
        console.error(`[FAIL] ${name}: ${err.message}`);
        failed++;
    }
}

/* -------------------------------------------------------------------------
 * Constant
 * ---------------------------------------------------------------------- */
test('Constant: integer value emits .0 suffix', () => {
    const out = generate(makeGraph('g', [constNode('k', 3)]), 'test');
    assert.ok(out.includes('float k_value = 3.0f;'), `unexpected output:\n${out}`);
});

test('Constant: fractional value emits as-is', () => {
    const out = generate(makeGraph('g', [constNode('k', 1.5)]), 'test');
    assert.ok(out.includes('float k_value = 1.5f;'), `unexpected output:\n${out}`);
});

test('Constant: negative value', () => {
    const out = generate(makeGraph('g', [constNode('k', -2.5)]), 'test');
    assert.ok(out.includes('float k_value = -2.5f;'), `unexpected output:\n${out}`);
});

test('Constant: zero', () => {
    const out = generate(makeGraph('g', [constNode('k', 0)]), 'test');
    assert.ok(out.includes('float k_value = 0.0f;'), `unexpected output:\n${out}`);
});

/* -------------------------------------------------------------------------
 * Add
 * ---------------------------------------------------------------------- */
test('Add: emits a + b', () => {
    const out = generate(binaryGraph('Add', 1, 2), 'test');
    assert.ok(out.includes('float out_value = a_value + b_value;'), `unexpected output:\n${out}`);
});

test('Add: rejects missing b edge', () => {
    const graph = makeGraph('g',
        [constNode('a', 1), { id: 'out', type: 'Add', params: {} }],
        [{ from: { node: 'a', port: 'value' }, to: { node: 'out', port: 'a' } }]
    );
    assert.throws(() => generate(graph, 'test'), /requires both 'a' and 'b'/);
});

test('Add: rejects missing a edge', () => {
    const graph = makeGraph('g',
        [constNode('b', 2), { id: 'out', type: 'Add', params: {} }],
        [{ from: { node: 'b', port: 'value' }, to: { node: 'out', port: 'b' } }]
    );
    assert.throws(() => generate(graph, 'test'), /requires both 'a' and 'b'/);
});

/* -------------------------------------------------------------------------
 * Subtract
 * ---------------------------------------------------------------------- */
test('Subtract: emits a - b', () => {
    const out = generate(binaryGraph('Subtract', 5, 3), 'test');
    assert.ok(out.includes('float out_value = a_value - b_value;'), `unexpected output:\n${out}`);
});

test('Subtract: rejects missing inputs', () => {
    const graph = makeGraph('g', [{ id: 'out', type: 'Subtract', params: {} }], []);
    assert.throws(() => generate(graph, 'test'), /requires both 'a' and 'b'/);
});

/* -------------------------------------------------------------------------
 * Multiply
 * ---------------------------------------------------------------------- */
test('Multiply: emits a * b', () => {
    const out = generate(binaryGraph('Multiply', 2, 3), 'test');
    assert.ok(out.includes('float out_value = a_value * b_value;'), `unexpected output:\n${out}`);
});

test('Multiply: rejects missing inputs', () => {
    const graph = makeGraph('g', [{ id: 'out', type: 'Multiply', params: {} }], []);
    assert.throws(() => generate(graph, 'test'), /requires both 'a' and 'b'/);
});

/* -------------------------------------------------------------------------
 * Gain
 * ---------------------------------------------------------------------- */
test('Gain: emits input * gain literal (fractional)', () => {
    const graph = makeGraph('g',
        [constNode('x', 1), { id: 'g', type: 'Gain', params: { gain: 2.5 } }],
        [{ from: { node: 'x', port: 'value' }, to: { node: 'g', port: 'input' } }]
    );
    const out = generate(graph, 'test');
    assert.ok(out.includes('float g_value = x_value * 2.5f;'), `unexpected output:\n${out}`);
});

test('Gain: integer gain uses .0 suffix', () => {
    const graph = makeGraph('g',
        [constNode('x', 1), { id: 'g', type: 'Gain', params: { gain: 3 } }],
        [{ from: { node: 'x', port: 'value' }, to: { node: 'g', port: 'input' } }]
    );
    const out = generate(graph, 'test');
    assert.ok(out.includes('float g_value = x_value * 3.0f;'), `unexpected output:\n${out}`);
});

test('Gain: negative gain', () => {
    const graph = makeGraph('g',
        [constNode('x', 1), { id: 'g', type: 'Gain', params: { gain: -1 } }],
        [{ from: { node: 'x', port: 'value' }, to: { node: 'g', port: 'input' } }]
    );
    const out = generate(graph, 'test');
    assert.ok(out.includes('float g_value = x_value * -1.0f;'), `unexpected output:\n${out}`);
});

test('Gain: rejects missing input edge', () => {
    const graph = makeGraph('g', [{ id: 'g', type: 'Gain', params: { gain: 1 } }], []);
    assert.throws(() => generate(graph, 'test'), /requires an 'input' edge/);
});

/* -------------------------------------------------------------------------
 * Saturation
 * ---------------------------------------------------------------------- */
test('Saturation: emits copy then min/max clamps', () => {
    const graph = makeGraph('g',
        [constNode('x', 0), { id: 'sat', type: 'Saturation', params: { min: -1, max: 1 } }],
        [{ from: { node: 'x', port: 'value' }, to: { node: 'sat', port: 'input' } }]
    );
    const out = generate(graph, 'test');
    assert.ok(out.includes('float sat_value = x_value;'), `copy line missing:\n${out}`);
    assert.ok(out.includes('if (sat_value < -1.0f) sat_value = -1.0f;'), `min clamp missing:\n${out}`);
    assert.ok(out.includes('if (sat_value > 1.0f) sat_value = 1.0f;'), `max clamp missing:\n${out}`);
});

test('Saturation: fractional bounds', () => {
    const graph = makeGraph('g',
        [constNode('x', 0), { id: 'sat', type: 'Saturation', params: { min: -0.5, max: 0.5 } }],
        [{ from: { node: 'x', port: 'value' }, to: { node: 'sat', port: 'input' } }]
    );
    const out = generate(graph, 'test');
    assert.ok(out.includes('if (sat_value < -0.5f) sat_value = -0.5f;'), `min clamp missing:\n${out}`);
    assert.ok(out.includes('if (sat_value > 0.5f) sat_value = 0.5f;'), `max clamp missing:\n${out}`);
});

test('Saturation: rejects missing input edge', () => {
    const graph = makeGraph('g', [{ id: 'sat', type: 'Saturation', params: { min: -1, max: 1 } }], []);
    assert.throws(() => generate(graph, 'test'), /requires an 'input' edge/);
});

/* -------------------------------------------------------------------------
 * KinematicsSolve removal verification
 * ---------------------------------------------------------------------- */
test('KinematicsSolve is rejected as unsupported type', () => {
    const graph = makeGraph('g',
        [{ id: 'k', type: 'KinematicsSolve', params: { mode: 'forward', robotModel: 'arm' } }],
        []
    );
    assert.throws(() => generate(graph, 'test'), /unsupported type/);
});

/* -------------------------------------------------------------------------
 * Summary
 * ---------------------------------------------------------------------- */
console.log(`\n=== graph_to_c tests: ${passed} passed, ${failed} failed ===`);
if (failed > 0) process.exit(1);
