#!/usr/bin/env node

/* Focused tests for the logical Time source and scoped CustomCode boundary. */
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const { generate } = require('../mbd/codegen/graph_to_c');

const root = path.resolve(__dirname, '..');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-time-custom-'));

function node(id, type, params) { return { id, type, params }; }
function edge(id, from, fromPort, to, toPort) {
  return { id, from: { node: from, port: fromPort }, to: { node: to, port: toPort } };
}
function graph(id, nodes, edges) {
  return { format: 'hypraccel.mbd.graph', version: 1, id, nodes, edges };
}

function check(name, value) {
  const json = path.join(work, `${name}.json`);
  const c = path.join(work, `${name}.c`);
  fs.writeFileSync(json, JSON.stringify(value, null, 2));
  const schema = spawnSync('python3', ['-c',
    "import json,sys,jsonschema; jsonschema.Draft202012Validator(json.load(open(sys.argv[1]))).validate(json.load(open(sys.argv[2])))",
    path.join(root, 'mbd/schema/graph.schema.json'), json], { encoding: 'utf8' });
  assert.strictEqual(schema.status, 0, `${name} schema validation failed: ${schema.stderr}`);
  const source = generate(value, path.basename(json));
  fs.writeFileSync(c, source);
  assert.strictEqual(source, generate(value, path.basename(json)), `${name} codegen is not deterministic`);
  const syntax = spawnSync('gcc', ['-std=c99', '-Wall', '-Werror', '-fsyntax-only', '-I', path.join(root, 'sdk/include'), c], { encoding: 'utf8' });
  assert.strictEqual(syntax.status, 0, `${name} generated C failed syntax check: ${syntax.stderr}`);
  return source;
}

const timeSource = check('time_multiply', graph('time_multiply', [
  node('time_1', 'Time', {}),
  node('scale', 'Constant', { value: 1.0 }),
  node('multiply', 'Multiply', {}),
  node('custom', 'CustomCode', { inputs: ['seconds'], code: 'printf("%f\\n", seconds);' })
], [
  edge('e1', 'time_1', 'value', 'multiply', 'a'),
  edge('e2', 'scale', 'value', 'multiply', 'b'),
  edge('e3', 'multiply', 'value', 'custom', 'seconds')
]));
assert.strictEqual((timeSource.match(/hyp_timestamp_us\(\)/g) || []).length, 1);
assert(timeSource.includes('float time_1_value = (float)hyp_timestamp_us() / 1000000.0f;'));
assert(timeSource.includes('const float seconds = multiply_value;'));
assert(timeSource.includes('printf("%f\\n", seconds);'));

const fourInputSource = check('four_input_custom', graph('four_input_custom', [
  node('base', 'Constant', { value: 90 }),
  node('shoulder', 'Constant', { value: 90 }),
  node('elbow', 'Constant', { value: 128 }),
  node('wrist', 'Constant', { value: 90 }),
  node('servo_packet', 'CustomCode', {
    inputs: ['base', 'shoulder', 'elbow', 'wrist'],
    code: 'printf("%d,%d,%d,%d\\n", (int)base, (int)shoulder, (int)elbow, (int)wrist);'
  })
], [
  edge('e1', 'base', 'value', 'servo_packet', 'base'),
  edge('e2', 'shoulder', 'value', 'servo_packet', 'shoulder'),
  edge('e3', 'elbow', 'value', 'servo_packet', 'elbow'),
  edge('e4', 'wrist', 'value', 'servo_packet', 'wrist')
]));
assert(fourInputSource.includes('const float base = base_value;'));
assert(fourInputSource.includes('printf("%d,%d,%d,%d\\n"'));

const rawControlSource = check('raw_control_chars_custom', graph('raw_control_chars_custom', [
  node('value', 'Constant', { value: 1 }),
  node('custom', 'CustomCode', {
    inputs: ['value'],
    code: String.raw`printf("line1
line2	%f \\ \"quoted\" \n", value);`
  })
], [edge('e1', 'value', 'value', 'custom', 'value')]));
const rawControlExpected = [
  'printf("line1', '\\n', 'line2', '\\t', '%f ', '\\\\', ' ', '\\"quoted\\"', ' ', '\\n', '", value);'
].join('');
assert(rawControlSource.includes(rawControlExpected));

console.log('[PASS] Time and CustomCode schema, deterministic codegen, SDK timestamp reuse, four-input formatting, and C syntax.');
