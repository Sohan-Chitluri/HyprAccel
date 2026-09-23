'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');

const { summarize } = require('../src/board_detail.js');

const YAML_PATH = path.join(__dirname, '..', '..', '..', 'boards', 'boards.yaml');

const doc = yaml.load(fs.readFileSync(YAML_PATH, 'utf8'));
const boards = doc.boards;

assert.ok(boards && boards.esp32, 'boards.yaml must contain an esp32 entry');
assert.ok(boards && boards.thejas32, 'boards.yaml must contain a thejas32 entry');

// --- summarize(null) ---------------------------------------------------
assert.strictEqual(summarize(null), null, 'summarize(null) must be null');
assert.strictEqual(summarize(undefined), null, 'summarize(undefined) must be null');

// --- esp32 ---------------------------------------------------------------
const esp32Raw = boards.esp32;
const esp32 = summarize(esp32Raw);

function countObjKeys(obj) {
  return obj ? Object.keys(obj).length : 0;
}

const expectedEsp32Counts = {
  gpio: (esp32Raw.pins.gpio || []).length,
  spi: countObjKeys(esp32Raw.pins.spi),
  i2c: countObjKeys(esp32Raw.pins.i2c),
  uart: countObjKeys(esp32Raw.pins.uart),
  pwm: (esp32Raw.pins.pwm || []).length,
  adc: (esp32Raw.pins.adc || []).length
};

assert.deepStrictEqual(esp32.counts, expectedEsp32Counts, 'esp32 counts must match yaml-derived expectations');

// i2c0 has device imu at 0x68, and 'devices' key must not be treated as a pin role.
const esp32I2c0 = esp32.buses.i2c.find((b) => b.instance === 'i2c0');
assert.ok(esp32I2c0, 'esp32 must have i2c0 bus');
assert.strictEqual(esp32I2c0.pins.scl, 'GPIO22');
assert.strictEqual(esp32I2c0.pins.sda, 'GPIO21');
assert.strictEqual(esp32I2c0.pins.devices, undefined, "'devices' must not leak into pins as a role");
assert.ok(Array.isArray(esp32I2c0.devices), 'i2c0 must expose a devices array');
const imu = esp32I2c0.devices.find((d) => d.name === 'imu');
assert.ok(imu, 'i2c0 devices must include imu');
assert.strictEqual(imu.address, '0x68', 'imu address must be formatted as 0x68 string');

assert.ok(esp32.accelerators.includes('CORDIC'), 'esp32 accelerators must include CORDIC');
assert.strictEqual(esp32.name, esp32Raw.name);
assert.strictEqual(esp32.architecture, esp32Raw.architecture);
assert.strictEqual(esp32.mcu, esp32Raw.mcu);
assert.strictEqual(esp32.clockMhz, esp32Raw.clock_freq_mhz);
assert.strictEqual(esp32.fpgaTransport, esp32Raw.fpga_transport);

// spi buses (hspi, vspi) carry sck/mosi/miso/cs
const esp32Hspi = esp32.buses.spi.find((b) => b.instance === 'hspi');
assert.ok(esp32Hspi, 'esp32 must have hspi bus');
assert.deepStrictEqual(esp32Hspi.pins, {
  sck: 'GPIO14',
  mosi: 'GPIO13',
  miso: 'GPIO12',
  cs: 'GPIO15'
});

// --- thejas32 --------------------------------------------------------------
const thejasRaw = boards.thejas32;
const thejas = summarize(thejasRaw);

const expectedThejasCounts = {
  gpio: (thejasRaw.pins.gpio || []).length,
  spi: countObjKeys(thejasRaw.pins.spi),
  i2c: countObjKeys(thejasRaw.pins.i2c),
  uart: countObjKeys(thejasRaw.pins.uart),
  pwm: (thejasRaw.pins.pwm || []).length,
  adc: (thejasRaw.pins.adc || []).length
};

assert.deepStrictEqual(thejas.counts, expectedThejasCounts, 'thejas32 counts must match yaml-derived expectations');
assert.ok(thejas.accelerators.includes('CORDIC'), 'thejas32 accelerators must include CORDIC');

// thejas32 has no mcu/board field — must be defensively handled, not throw.
assert.strictEqual(thejas.mcu, '', 'thejas32 has no mcu field; summarize should default to empty string');

// thejas32 i2c buses have no devices — devices array must be empty, not undefined.
const thejasI2c0 = thejas.buses.i2c.find((b) => b.instance === 'i2c0');
assert.ok(thejasI2c0);
assert.deepStrictEqual(thejasI2c0.devices, []);
assert.deepStrictEqual(thejasI2c0.pins, { scl: 'IIC0SCL', sda: 'IIC0SDA' });

console.log('All board_detail tests passed.');
console.log('esp32 counts:', esp32.counts);
console.log('thejas32 counts:', thejas.counts);
