const fs = require('fs');
const path = require('path');

const REPO_ROOT = '/home/peskybird/Projects/HyprAccel';
const BOARDS_YAML = path.join(REPO_ROOT, 'boards/boards.yaml');
const HARDWARE_CONFIG = path.join(REPO_ROOT, '.hypraccel/hardware.json');
const SERVER_FILE = path.join(REPO_ROOT, 'mbd/editor/server.js');

console.log('=== TESTING THE FIX ===\n');

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

// Scenario: User has a hardware.json created for esp32 (with pwm.GPIO25, etc.)
// but the stored board is thejas32 (which uses pwm.PWM0, etc.)
// This used to cause "Unknown hardware resource" during migration

console.log('--- Test: Hardware config with board=thejas32 but resources from esp32 ---');
const storedHwConfig = {
    version: 1,
    board: 'thejas32',  // But resources are from esp32!
    resources: {
        'spi.hspi': { id: 'spi.hspi', type: 'spi', instance: 'hspi', available: true, assignments: [], configuration: {} },
        'pwm.GPIO25': { id: 'pwm.GPIO25', type: 'pwm', instance: 'GPIO25', available: true, assignments: [], configuration: {} },
        'pwm.GPIO26': { id: 'pwm.GPIO26', type: 'pwm', instance: 'GPIO26', available: true, assignments: [], configuration: {} },
        'accelerator.cordic': { id: 'accelerator.cordic', type: 'accelerator', instance: 'cordic', available: true, assignments: [], configuration: {} }
    },
    assignments: [],
    devices: []
};

fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(storedHwConfig, null, 2), 'utf8');

console.log('Wrote mixed hardware config (board=thejas32, esp32 resources)');

try {
    const hw = readHardwareConfig();
    console.log('Migrated board:', hw.board);
    console.log('Has accelerator.cordic:', !!hw.resources['accelerator.cordic']);
    console.log('Has spi.hspi:', !!hw.resources['spi.hspi']);
    console.log('Has pwm.GPIO25:', !!hw.resources['pwm.GPIO25']);
    console.log('Has pwm.PWM0:', !!hw.resources['pwm.PWM0']);
    console.log('\n[PASS] Migration succeeded without "Unknown hardware resource" error!');
} catch (e) {
    console.log('[FAIL] Migration failed:', e.message);
}

console.log('\n--- Test: Graph with targetBoard=thejas32, hardware has thejas32 board with CORDIC ---');
const graphThejas = {
    format: 'hypraccel.mbd.graph',
    version: 1,
    id: 'test_cordic',
    metadata: { targetBoard: 'thejas32' },
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
        targetBoard: 'thejas32',
        externalBindings: [
            { input: 'angle', to: { node: 'CordicOp_1', port: 'angle_rad' } }
        ]
    }
};

try {
    validateGraphHardwareResources(graphThejas);
    console.log('[PASS] Graph validated successfully!');
} catch (e) {
    console.log('[FAIL] Graph validation failed:', e.message);
}

console.log('\n=== DONE ===');