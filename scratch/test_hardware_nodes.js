#!/usr/bin/env node

/* Focused NL/CG tests for the existing GPIOInput, ADCInput and PWMOutput nodes. */
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const root = path.resolve(__dirname, '..');
const codegen = path.join(root, 'mbd', 'codegen', 'graph_to_c.js');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-hw-nodes-'));

function node(id, type, params) { return { id, type, params }; }
function edge(id, from, fromPort, to, toPort) {
  return { id, from: { node: from, port: fromPort }, to: { node: to, port: toPort } };
}
function graph(id, nodes, edges) {
  return { format: 'hypraccel.mbd.graph', version: 1, id, nodes, edges };
}

const cases = [
  ['constant_pwm', graph('constant_pwm', [
    node('constant', 'Constant', { value: 128 }),
    node('pwm', 'PWMOutput', { hardwareResource: 'pwm.GPIO25', min: 0, max: 255, unit: '%', safeValue: 0 })
  ], [edge('e1', 'constant', 'value', 'pwm', 'value')])],
  ['gpio_publish', graph('gpio_publish', [
    node('gpio', 'GPIOInput', { hardwareResource: 'gpio.GPIO4', samplePeriodUs: 1000, invert: true }),
    node('publish', 'Publish', { topic: 'gpio/value', transport: 'host' })
  ], [edge('e1', 'gpio', 'value', 'publish', 'value')])],
  ['adc_gain_publish', graph('adc_gain_publish', [
    node('adc', 'ADCInput', { hardwareResource: 'adc.GPIO32', samplePeriodUs: 1000, minValue: 0, maxValue: 3.3 }),
    node('gain', 'Gain', { gain: 2 }),
    node('publish', 'Publish', { topic: 'adc/value', transport: 'host' })
  ], [edge('e1', 'adc', 'value', 'gain', 'input'), edge('e2', 'gain', 'value', 'publish', 'value')])],
  ['adc_gain_pwm', graph('adc_gain_pwm', [
    node('adc', 'ADCInput', { hardwareResource: 'adc.GPIO32', minValue: 0, maxValue: 3.3 }),
    node('gain', 'Gain', { gain: 1 }),
    node('pwm', 'PWMOutput', { hardwareResource: 'pwm.GPIO25', min: 0, max: 3.3, safeValue: 0 })
  ], [edge('e1', 'adc', 'value', 'gain', 'input'), edge('e2', 'gain', 'value', 'pwm', 'value')])],
  ['gpio_switch_pwm', graph('gpio_switch_pwm', [
    node('gpio', 'GPIOInput', { hardwareResource: 'gpio.GPIO4', invert: false }),
    node('on', 'Constant', { value: 255 }),
    node('off', 'Constant', { value: 0 }),
    node('selector', 'Switch', {}),
    node('pwm', 'PWMOutput', { hardwareResource: 'pwm.GPIO25', min: 0, max: 255, safeValue: 0 })
  ], [edge('e1', 'gpio', 'value', 'selector', 'condition'), edge('e2', 'on', 'value', 'selector', 'true_value'),
      edge('e3', 'off', 'value', 'selector', 'false_value'), edge('e4', 'selector', 'value', 'pwm', 'value')])]
];

function generate(name, value) {
  const input = path.join(work, `${name}.json`);
  const output = path.join(work, `${name}.c`);
  fs.writeFileSync(input, JSON.stringify(value, null, 2));
  const schemaCheck = spawnSync('python3', ['-c',
    "import json,sys,jsonschema; schema=json.load(open(sys.argv[1])); graph=json.load(open(sys.argv[2])); jsonschema.Draft202012Validator(schema).validate(graph)",
    path.join(root, 'mbd', 'schema', 'graph.schema.json'), input], { encoding: 'utf8' });
  assert.strictEqual(schemaCheck.status, 0, `${name} schema validation failed: ${schemaCheck.stderr}`);
  const result = spawnSync(process.execPath, [codegen, input, output], { encoding: 'utf8' });
  assert.strictEqual(result.status, 0, `${name} codegen failed: ${result.stderr}`);
  const source = fs.readFileSync(output, 'utf8');
  const second = path.join(work, `${name}.second.c`);
  const repeat = spawnSync(process.execPath, [codegen, input, second], { encoding: 'utf8' });
  assert.strictEqual(repeat.status, 0);
  assert.strictEqual(source, fs.readFileSync(second, 'utf8'), `${name} output is not deterministic`);
  assert(!/digitalRead|analogRead|ledcWrite/.test(source), `${name} bypasses the SDK abstraction`);
  const syntax = spawnSync('gcc', ['-std=c99', '-Wall', '-Werror', '-fsyntax-only', '-I', path.join(root, 'sdk', 'include'), output], { encoding: 'utf8' });
  assert.strictEqual(syntax.status, 0, `${name} generated C failed syntax check: ${syntax.stderr}`);
  return source;
}

const outputs = Object.fromEntries(cases.map(([name, value]) => [name, generate(name, value)]));
assert(outputs.constant_pwm.includes('hyp_actuator_write("pwm.GPIO25"'));
assert(outputs.gpio_publish.includes('hyp_sensor_read("gpio.GPIO4"'));
assert(outputs.gpio_publish.includes('!gpio_value'));
assert(outputs.adc_gain_publish.includes('hyp_sensor_read("adc.GPIO32"'));
assert(outputs.gpio_switch_pwm.includes('hyp_actuator_write("pwm.GPIO25"'));

const invalid = JSON.parse(JSON.stringify(cases[0][1]));
invalid.nodes[1].params.hardwareResource = 'adc.GPIO32';
const invalidInput = path.join(work, 'invalid.json');
const invalidOutput = path.join(work, 'invalid.c');
fs.writeFileSync(invalidInput, JSON.stringify(invalid));
const rejected = spawnSync(process.execPath, [codegen, invalidInput, invalidOutput], { encoding: 'utf8' });
assert.notStrictEqual(rejected.status, 0);
assert(`${rejected.stderr}\n${rejected.stdout}`.length > 0, 'invalid resource rejection did not report a diagnostic');

console.log(`[PASS] ${cases.length} hardware I/O graph cases: schema-shaped params, canonical resources, deterministic SDK codegen, C syntax, invalid-resource diagnostics.`);
