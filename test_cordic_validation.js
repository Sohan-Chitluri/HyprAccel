const fs = require('fs');
const path = require('path');

const REPO_ROOT = '/home/peskybird/Projects/HyprAccel';
console.log('REPO_ROOT:', REPO_ROOT);
const BOARDS_YAML = path.join(REPO_ROOT, 'boards/boards.yaml');
const HARDWARE_CONFIG = path.join(REPO_ROOT, '.hypraccel/hardware.json');
const SERVER_FILE = path.join(REPO_ROOT, 'mbd/editor/server.js');
console.log('SERVER_FILE:', SERVER_FILE);

console.log('=== TESTING CORDIC ACCELERATOR VALIDATION ===\n');

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

// Test: Create hardware config with accelerator.cordic
const hwConfig = projectHardware('esp32', []);
console.log('Created hardware config with board: esp32');
console.log('Has accelerator.cordic:', !!hwConfig.resources['accelerator.cordic']);
if (hwConfig.resources['accelerator.cordic']) {
    console.log('accelerator.cordic type:', hwConfig.resources['accelerator.cordic'].type);
    console.log('accelerator.cordic assignments:', hwConfig.resources['accelerator.cordic'].assignments);
}

// Write it to the temp config
fs.mkdirSync(path.dirname(HARDWARE_CONFIG), { recursive: true });
fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(hwConfig, null, 2), 'utf8');
console.log('\nWrote hardware config to:', HARDWARE_CONFIG);

// Test graph with CordicOp using accelerator.cordic
const cordicGraph = {
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
    validateGraphHardwareResources(cordicGraph);
    console.log('\n[PASS] Graph with CordicOp and accelerator.cordic validated successfully!');
} catch (e) {
    console.error('\n[FAIL] Graph validation failed:', e.message);
}

// Also test: What if hardware config doesn't have accelerator.cordic?
console.log('\n--- Testing without accelerator in hardware config ---');

// Create a config without accelerator
const boards = parseSimpleYaml(fs.readFileSync(BOARDS_YAML, 'utf8')).boards;
const esp32Board = boards.esp32;
const resourcesWithoutAccel = defaultResourceConfig({ ...esp32Board, accelerators: [] });
console.log('Resources without accelerator:', Object.keys(resourcesWithoutAccel).filter(k => k.startsWith('accelerator')));

const hwConfigNoAccel = projectHardware('esp32', [], {}, [], { ...esp32Board, accelerators: [] });
// Actually projectHardware uses defaultResourceConfig which reads from board.accelerators
// Let's test manually

console.log('\n--- Testing projectHardware with board that has no accelerators ---');
const boardNoAccel = { ...esp32Board, accelerators: [] };
const hwNoAccel = projectHardware('esp32', [], {}, [], boardNoAccel);
console.log('Has accelerator.cordic:', !!hwNoAccel.resources['accelerator.cordic']);