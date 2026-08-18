const fs = require('fs');
const path = require('path');

const REPO_ROOT = '/home/peskybird/Projects/HyprAccel';
const BOARDS_YAML = path.join(REPO_ROOT, 'boards/boards.yaml');
const HARDWARE_CONFIG = path.join(REPO_ROOT, '.hypraccel/hardware.json');
const SERVER_FILE = path.join(REPO_ROOT, 'mbd/editor/server.js');

console.log('=== TESTING STALE HARDWARE CONFIG SCENARIO ===\n');

// Extract functions from server.js
const serverCode = fs.readFileSync(SERVER_FILE, 'utf8');

const funcExtractor = new Function('fs', 'path', 'BOARDS_YAML', 'HARDWARE_CONFIG', `
    ${serverCode.match(/const RESOURCE_SIGNAL_ROLES[\s\S]*?\n\}\);/)[0]}
    ${serverCode.match(/const LEGACY_SIGNAL_ROLES[\s\S]*?\n\}\);/)[0]}
    ${serverCode.match(/function parseSimpleYaml[\s\S]*?\n\}/)[0]}
    ${serverCode.match(/function defaultResourceConfig[\s\S]*?\n\}/)[0]}
    ${serverCode.match(/function projectHardware[\s\S]*?\n\}/)[0]}
    ${serverCode.match(/function validateGraphHardwareResources[\s\S]*?\n\}/)[0]}
    ${serverCode.match(/function readHardwareConfig[\s\S]*?\n\}/)[0]}
    return { RESOURCE_SIGNAL_ROLES, LEGACY_SIGNAL_ROLES, parseSimpleYaml, defaultResourceConfig, projectHardware, validateGraphHardwareResources, readHardwareConfig };
`);

const {
    RESOURCE_SIGNAL_ROLES,
    LEGACY_SIGNAL_ROLES,
    parseSimpleYaml,
    defaultResourceConfig,
    projectHardware,
    validateGraphHardwareResources,
    readHardwareConfig
} = funcExtractor(fs, path, BOARDS_YAML, HARDWARE_CONFIG);

// Scenario: User has a hardware.json with a board that doesn't have CORDIC
// but the graph has targetBoard that does (or vice versa)

console.log('--- Test 1: Hardware config with thejas32 board (has CORDIC) ---');
const hwConfigThejas = projectHardware('thejas32', []);
console.log('Board:', hwConfigThejas.board);
console.log('Has accelerator.cordic:', !!hwConfigThejas.resources['accelerator.cordic']);

// Save this as hardware.json
fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(hwConfigThejas, null, 2), 'utf8');

console.log('\n--- Test 2: Graph with targetBoard=esp32, but hardware.json has thejas32 ---');
const graphEsp32 = {
    format: 'hypraccel.mbd.graph',
    version: 1,
    id: 'test_cordic',
    metadata: { targetBoard: 'esp32' },
    inputs: [{ name: 'angle', type: 'number' }],
    nodes: [
        {
            id: 'CordicOp_1',
            type: 'CordicOp',
            params: {
                operation: 'sin',
                implementation: 'auto',
                iterations: 16,
                hardwareResource: 'accelerator.cordic'
            }
        }
    ],
    edges: [],
    metadata: {
        targetBoard: 'esp32',
        externalBindings: [
            { input: 'angle', to: { node: 'CordicOp_1', port: 'angle_rad' } }
        ]
    }
};

try {
    validateGraphHardwareResources(graphEsp32);
    console.log('[PASS] Graph validated');
} catch (e) {
    console.log('[EXPECTED FAIL] Graph validation failed:', e.message);
}

console.log('\n--- Test 3: Graph with targetBoard=thejas32, hardware.json has thejas32 ---');
const graphThejas = {
    ...graphEsp32,
    metadata: { ...graphEsp32.metadata, targetBoard: 'thejas32' }
};
try {
    validateGraphHardwareResources(graphThejas);
    console.log('[PASS] Graph validated');
} catch (e) {
    console.log('[FAIL] Graph validation failed:', e.message);
}

console.log('\n--- Test 4: Graph with targetBoard=esp32, hardware.json has esp32 ---');
const hwConfigEsp32 = projectHardware('esp32', []);
fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(hwConfigEsp32, null, 2), 'utf8');
try {
    validateGraphHardwareResources(graphEsp32);
    console.log('[PASS] Graph validated');
} catch (e) {
    console.log('[FAIL] Graph validation failed:', e.message);
}

console.log('\n--- Test 5: What if hardware.json has accelerator.cordic but boards.yaml board does NOT have it? ---');
// Create a fake board without CORDIC
const parsed = parseSimpleYaml(fs.readFileSync(BOARDS_YAML, 'utf8'));
const fakeBoard = { ...parsed.boards.esp32, accelerators: [] };
const resourcesNoCordic = defaultResourceConfig(fakeBoard);
console.log('Resources without CORDIC:', Object.keys(resourcesNoCordic).filter(k => k.startsWith('accelerator')));

// Now try to use projectHardware with this board - it should not have accelerator.cordic
const hwNoCordic = projectHardware('esp32', [], {}, [], fakeBoard);
console.log('Has accelerator.cordic:', !!hwNoCordic.resources['accelerator.cordic']);

// What if someone tries to assign a pin to accelerator.cordic in hardware config?
console.log('\n--- Test 6: Try to assign pin to accelerator.cordic (should fail) ---');
try {
    projectHardware('esp32', [
        { node: 'CordicOp[1]', role: 'input', pin: 'GPIO1', resource: 'accelerator.cordic' }
    ]);
    console.log('[FAIL] Should have rejected pin assignment to accelerator');
} catch (e) {
    console.log('[PASS] Correctly rejected:', e.message);
}

// What about the error "Unknown hardware resource 'accelerator.cordic'"?
// This would come from line 353 when resources[resourceId] is falsy
// That would happen if someone passes a resourceId that's not in defaultResourceConfig
console.log('\n--- Test 7: Unknown hardware resource error ---');
try {
    projectHardware('esp32', [
        { node: 'SensorInput[1]', role: 'mosi', pin: 'GPIO13', resource: 'accelerator.cordic' }
    ]);
    console.log('[FAIL] Should have rejected');
} catch (e) {
    console.log('Error:', e.message);
}

console.log('\n=== DONE ===');