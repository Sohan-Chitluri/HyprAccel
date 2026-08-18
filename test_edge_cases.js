const fs = require('fs');
const path = require('path');

const REPO_ROOT = '/home/peskybird/Projects/HyprAccel';
const BOARDS_YAML = path.join(REPO_ROOT, 'boards/boards.yaml');
const HARDWARE_CONFIG = path.join(REPO_ROOT, '.hypraccel/hardware.json');
const SERVER_FILE = path.join(REPO_ROOT, 'mbd/editor/server.js');

console.log('=== TESTING EDGE CASES FOR CORDIC ERROR ===\n');

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

// Test: Graph with NO targetBoard in metadata, but hardware.json has board=esp32
console.log('--- Test 1: Graph with no targetBoard, hardware has esp32 ---');
const hwConfigEsp32 = projectHardware('esp32', []);
fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(hwConfigEsp32, null, 2), 'utf8');

const graphNoTargetBoard = {
    format: 'hypraccel.mbd.graph',
    version: 1,
    id: 'test_cordic',
    metadata: {},  // No targetBoard!
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
        externalBindings: [
            { input: 'angle', to: { node: 'CordicOp_1', port: 'angle_rad' } }
        ]
    }
};

try {
    validateGraphHardwareResources(graphNoTargetBoard);
    console.log('[PASS] Graph validated');
} catch (e) {
    console.log('[FAIL] Graph validation failed:', e.message);
}

// Test: Graph with targetBoard but hardware.json has board=null (default/empty)
console.log('\n--- Test 2: Graph with targetBoard=esp32, hardware has board=null ---');
const emptyHwConfig = { version: 1, board: null, resources: {}, assignments: [] };
fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(emptyHwConfig, null, 2), 'utf8');

const graphWithTarget = {
    ...graphNoTargetBoard,
    metadata: { 
        targetBoard: 'esp32',
        externalBindings: [
            { input: 'angle', to: { node: 'CordicOp_1', port: 'angle_rad' } }
        ]
    }
};

try {
    validateGraphHardwareResources(graphWithTarget);
    console.log('[PASS] Graph validated');
} catch (e) {
    console.log('[FAIL] Graph validation failed:', e.message);
}

// Test: What if hardware.json has board=esp32 but resources is empty?
console.log('\n--- Test 3: Graph with targetBoard=esp32, hardware has board=esp32 but empty resources ---');
const hwEmptyResources = { version: 1, board: 'esp32', resources: {}, assignments: [] };
fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(hwEmptyResources, null, 2), 'utf8');

try {
    validateGraphHardwareResources(graphWithTarget);
    console.log('[PASS] Graph validated');
} catch (e) {
    console.log('[FAIL] Graph validation failed:', e.message);
}

// Test: What if the hardware.json has accelerator.cordic in resources but it's somehow not being picked up?
console.log('\n--- Test 4: Check readHardwareConfig behavior ---');
fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(hwConfigEsp32, null, 2), 'utf8');
const hw = readHardwareConfig();
console.log('readHardwareConfig board:', hw.board);
console.log('readHardwareConfig resources has accelerator.cordic:', !!hw.resources['accelerator.cordic']);
console.log('readHardwareConfig assignments:', hw.assignments);

// Test: What about the "configured" set logic?
console.log('\n--- Test 5: Check configured set logic ---');
const configured = new Set((hw.assignments || []).map(assignment => assignment.resource));
console.log('configured from assignments:', Array.from(configured));

for (const [id, resource] of Object.entries(hw.resources || {})) {
    if (resource.type === 'accelerator' || (resource.configuration && Object.keys(resource.configuration).length > 0)) {
        configured.add(id);
    }
}
console.log('configured after adding accelerators:', Array.from(configured));

// Test: The exact validation path for CordicOp
console.log('\n--- Test 6: Exact validation path for CordicOp ---');
const resourceId = 'accelerator.cordic';
const expectedType = 'accelerator';
console.log('resourceId.startsWith(`${expectedType}.`)', resourceId.startsWith(`${expectedType}.`));
console.log('hw.resources[resourceId]:', !!hw.resources[resourceId]);
console.log('configured.has(resourceId):', configured.has(resourceId));
console.log('All conditions:', !!hw.resources && !!hw.resources[resourceId] && configured.has(resourceId));

// Test: What if hardware config was created with a different board and migrated?
console.log('\n--- Test 7: Hardware config migration scenario ---');
// Simulate an old hardware.json that was created with thejas32 but now we use esp32
const oldHwConfig = {
    version: 1,
    board: 'thejas32',
    resources: hwConfigEsp32.resources,  // Use esp32 resources but thejas32 board
    assignments: []
};
fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(oldHwConfig, null, 2), 'utf8');

try {
    const hw2 = readHardwareConfig();
    console.log('Migrated hardware board:', hw2.board);
    console.log('Migrated resources has accelerator.cordic:', !!hw2.resources['accelerator.cordic']);
} catch (e) {
    console.log('Migration error:', e.message);
}

console.log('\n=== DONE ===');