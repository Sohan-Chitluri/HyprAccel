/**
 * test_easyeda_import.js — HyprAccel EasyEDA .tel import test suite
 *
 * Verifies the EasyEDA netlist importer on the real fixture when available.
 * This script intentionally does not fabricate a schematic/netlist fixture.
 */

const fs = require('fs');
const path = require('path');

const {
    parseEasyEDANetlist,
    easyedaToNormalizedModel,
    importEasyEDASchematic
} = require('../mbd/schematic/easyeda_parser');
const {
    resolveTargetMcu
} = require('../mbd/schematic/mcu_resolver');
const {
    generateHardwareResourceModel,
    inferSignalRole
} = require('../mbd/schematic/resource_mapper');
const {
    importEasyEDA
} = require('../mbd/schematic');

const REPO_ROOT = path.resolve(__dirname, '..');
const DEFAULT_FIXTURES = [
    path.join(REPO_ROOT, 'Netlist_Schematic1_2026-08-18.tel'),
    path.join(REPO_ROOT, 'scratch', 'Netlist_Schematic1_2026-08-18.tel'),
    path.join(process.cwd(), 'Netlist_Schematic1_2026-08-18.tel')
];

function findTelFixture() {
    const explicit = process.env.TEL_FIXTURE_PATH;
    if (explicit && fs.existsSync(explicit)) return explicit;
    for (const candidate of DEFAULT_FIXTURES) {
        if (fs.existsSync(candidate)) return candidate;
    }
    throw new Error(
        'Missing real EasyEDA fixture Netlist_Schematic1_2026-08-18.tel. ' +
        'Set TEL_FIXTURE_PATH or place the file in the repository root.'
    );
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function summarizeAssignments(assignments) {
    return assignments.map(a => `${a.resource}.${a.role}=${a.pin}`).sort();
}

console.log('=== HYPRACCEL EASYEDA .TEL IMPORT TEST SUITE ===\n');

const fixturePath = findTelFixture();
console.log(`Using fixture: ${fixturePath}\n`);

const telContent = fs.readFileSync(fixturePath, 'utf8');

// 1. Parse .tel successfully
const rawParsed = parseEasyEDANetlist(telContent);
assert(rawParsed && Array.isArray(rawParsed.components) && rawParsed.components.length > 0,
    'Parsed EasyEDA .tel fixture with at least one component.');
assert(Array.isArray(rawParsed.nets) && rawParsed.nets.length > 0,
    'Parsed EasyEDA .tel fixture with at least one net.');

// 2. Components and nets extracted
const refs = rawParsed.components.map(c => c.reference);
assert(refs.some(ref => /^U\d+/i.test(ref)), 'Extracted at least one MCU-like component reference.');

// 3. Canonical model produced
const { normalizedModel } = importEasyEDASchematic(telContent);
assert(normalizedModel && Array.isArray(normalizedModel.symbols) && normalizedModel.symbols.length > 0,
    'Produced canonical normalized schematic model with symbols.');
assert(Array.isArray(normalizedModel.nets) && normalizedModel.nets.length > 0,
    'Produced canonical normalized schematic model with nets.');
assert(typeof normalizedModel.findTrace === 'function',
    'Canonical model exposes trace lookup.');

// 4. ESP32 identified
const targetResolution = resolveTargetMcu(normalizedModel);
assert(targetResolution.status === 'resolved' && targetResolution.boardKey === 'esp32',
    `Identified ESP32 target MCU (resolved boardKey=${targetResolution.boardKey || 'n/a'}).`);
assert(targetResolution.mcuSymbol && targetResolution.mcuSymbol.reference,
    'Resolved target MCU symbol reference.');

// 5. Physical MCU pin numbers extracted
const mcuRef = targetResolution.mcuSymbol.reference;
const mcuSymbol = normalizedModel.symbolsMap.get(mcuRef);
assert(mcuSymbol && Array.isArray(mcuSymbol.pins) && mcuSymbol.pins.length > 0,
    'Extracted MCU pins from the EasyEDA netlist.');
assert(Object.keys(targetResolution.pinMap || {}).length > 0,
    'Recovered physical MCU pin mapping.');

// 6. I2C SDA/SCL extracted and mapped
const i2cAssignments = [];
for (const pin of mcuSymbol.pins) {
    const contextText = `${pin.name} ${pin.number} ${(pin.nets || []).join(' ')}`;
    const inferred = inferSignalRole(contextText);
    if (inferred && inferred.type === 'i2c') {
        i2cAssignments.push({
            node: `Schematic:${mcuRef}:${pin.name || pin.number}`,
            role: inferred.role,
            pin: targetResolution.pinMap[pin.number] || targetResolution.pinMap[pin.name] || pin.name,
            resource: 'i2c.i2c0'
        });
    }
}

const i2cResourcePreview = generateHardwareResourceModel(normalizedModel, targetResolution);
assert(i2cResourcePreview.status === 'resolved' && i2cResourcePreview.hardware,
    'Generated canonical hardware model from the EasyEDA import.');

const imported = importEasyEDA(telContent);
assert(imported.status === 'success' && imported.hardware,
    'Full EasyEDA import pipeline completed successfully.');

const assignmentSummary = summarizeAssignments(imported.hardware.assignments || []);
assert(imported.hardware.resources['i2c.i2c0'] && imported.hardware.resources['i2c.i2c0'].available === true,
    'Resource mapper produced configured i2c.i2c0 hardware resource.');
assert(imported.hardware.assignments.some(a => a.resource === 'i2c.i2c0' && a.role === 'sda'),
    `Detected I2C SDA assignment in canonical hardware model. Assignments: ${assignmentSummary.join(', ')}`);
assert(imported.hardware.assignments.some(a => a.resource === 'i2c.i2c0' && a.role === 'scl'),
    `Detected I2C SCL assignment in canonical hardware model. Assignments: ${assignmentSummary.join(', ')}`);

// 7. Preserve physical MCU pin identities for conflict detection
const assignedPins = imported.hardware.assignments.map(a => a.pin);
assert(new Set(assignedPins).size === assignedPins.length,
    'Canonical hardware model preserves unique physical pin assignments without silent deduplication.');

// 8. Malformed input rejected
let malformedRejected = false;
try {
    parseEasyEDANetlist('not a tel netlist');
} catch (err) {
    malformedRejected = true;
}
assert(malformedRejected, 'Malformed EasyEDA input is rejected.');

// 9. Do not classify AIN1/AIN2/BIN1/BIN2/STBY as ADC by accident
const rawNetNames = (rawParsed.nets || []).map(n => String(n.name || '').toUpperCase());
const suspectNames = rawNetNames.filter(name => /(AIN\d+|BIN\d+|STBY)/.test(name));
if (suspectNames.length > 0) {
    for (const name of suspectNames) {
        const role = inferSignalRole(name);
        assert(!role || role.type !== 'adc',
            `Signal name "${name}" must not be misclassified as ADC.`);
    }
}

console.log('Assignments:', assignmentSummary.join(', '));
console.log(`I2C preview assignments: ${JSON.stringify(i2cAssignments, null, 2)}`);
console.log('\n=== ALL EASYEDA TEST CHECKS COMPLETE ===');
