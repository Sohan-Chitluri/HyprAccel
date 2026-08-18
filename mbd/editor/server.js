/**
 * server.js — HyprAccel MBD Editor Dev Server
 * MBD-T1a / MBD-T1b / MBD-T5 / MBD-T6
 *
 * Endpoints:
 *   GET  /api/boards          → parsed boards.yaml as JSON
 *   POST /api/generate        → write pin assignments → call gen_board_config.js → return header
 *   GET  /                    → serves pin_config.html
 *   GET  /graph                → serves the node graph editor
 *   POST /api/build           → run graph_to_c.js for the current graph
 */

const express = require('express');
const fs      = require('fs');
const path    = require('path');
const os           = require('os');
const { execFileSync, execSync, spawnSync } = require('child_process');

const app  = express();
const PORT = Number(process.env.HYPRACCEL_EDITOR_PORT || 3737);
const HOST = process.env.HYPRACCEL_EDITOR_HOST || '127.0.0.1';

const REPO_ROOT    = path.resolve(__dirname, '../../');
const BOARDS_YAML  = path.join(REPO_ROOT, 'boards/boards.yaml');
const CODEGEN_JS   = path.join(REPO_ROOT, 'boards/codegen/gen_board_config.js');
const CODEGEN_OUT  = path.join(REPO_ROOT, 'boards/codegen');
const GRAPH_CODEGEN = path.join(REPO_ROOT, 'mbd/codegen/graph_to_c.js');
const ESP32_PROJECT = path.join(REPO_ROOT, 'mbd/esp32');
const ESP32_GENERATED = path.join(ESP32_PROJECT, 'generated');
const ESP32_PORT = process.env.HYPRACCEL_ESP32_PORT || '';
const HARDWARE_CONFIG = path.join(REPO_ROOT, '.hypraccel', 'hardware.json');
const RESOURCE_SIGNAL_ROLES = Object.freeze({
    spi: new Set(['sck', 'mosi', 'miso', 'cs']),
    uart: new Set(['tx', 'rx']),
    i2c: new Set(['sda', 'scl']),
    pwm: new Set(['output']),
    adc: new Set(['input']),
    gpio: new Set(['gpio'])
});

const LEGACY_SIGNAL_ROLES = Object.freeze({
    spi: { bus: 'mosi', data: 'miso', clk: 'sck', cs: 'cs' },
    uart: { bus: 'tx', data: 'rx' },
    i2c: { bus: 'sda', data: 'scl', clk: 'scl' },
    pwm: { bus: 'output' },
    adc: { bus: 'input' },
    gpio: { bus: 'gpio', data: 'gpio', clk: 'gpio', cs: 'gpio' }
});

app.use(express.json());
app.use(express.static(path.join(__dirname, 'src')));

app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'src/pin_config.html'));
});

app.get('/graph', (req, res) => {
    res.sendFile(path.join(__dirname, 'src/graph_editor.html'));
});

app.get('/workspace', (req, res) => {
    res.sendFile(path.join(__dirname, 'src/workspace.html'));
});

/* --------------------------------------------------------------------------
 * YAML parser (mirrors the logic in gen_board_config.js — no npm yaml dep)
 * ----------------------------------------------------------------------- */
function parseSimpleYaml(content) {
    const lines = content.split('\n');
    const result = { boards: {} };
    let currentBoard = null;
    let currentCategory = null;
    let currentPinSection = null;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].split('#')[0].trimEnd();
        if (line.trim().length === 0) continue;

        const indent  = line.search(/\S/);
        const trimmed = line.trim();

        if (indent === 0 && trimmed === 'boards:') continue;

        if (indent === 2 && trimmed.endsWith(':')) {
            currentBoard = trimmed.slice(0, -1);
            result.boards[currentBoard] = {
                accelerators: [],
                pins: { gpio: [], spi: {}, i2c: {}, uart: {}, pwm: [], adc: [] }
            };
            currentCategory = null;
            currentPinSection = null;
            continue;
        }

        if (indent === 4 && currentBoard) {
            if (trimmed === 'pins:') { currentCategory = 'pins'; continue; }
            if (trimmed === 'accelerators:') { currentCategory = 'accelerators'; currentPinSection = null; continue; }
            if (trimmed.endsWith(':') && currentCategory !== 'pins') {
                currentCategory = trimmed.slice(0, -1);
            } else if (trimmed.includes(':') && currentCategory !== 'accelerators' && currentCategory !== 'pins') {
                const [key, ...rest] = trimmed.split(':');
                let val = rest.join(':').trim();
                if (val.startsWith('"') && val.endsWith('"')) val = val.slice(1, -1);
                else if (!isNaN(Number(val))) val = Number(val);
                result.boards[currentBoard][key.trim()] = val;
            }
            continue;
        }

        if (indent === 6 && currentBoard) {
            if (currentCategory === 'accelerators' && trimmed.startsWith('- "')) {
                result.boards[currentBoard].accelerators.push(trimmed.slice(3, -1));
                continue;
            }
            if (currentCategory === 'pins') {
                if (trimmed.endsWith(':')) { currentPinSection = trimmed.slice(0, -1); continue; }
            }
        }

        if (indent === 8 && currentBoard && currentCategory === 'pins' && currentPinSection) {
            const sec = currentPinSection;
            if (trimmed.startsWith('- "')) {
                const val = trimmed.slice(3, -1);
                if (['gpio', 'pwm', 'adc'].includes(sec)) {
                    result.boards[currentBoard].pins[sec].push(val);
                }
            } else if (trimmed.includes(': {')) {
                const [key, valStr] = trimmed.split(': {');
                const cleanValStr = valStr.replace('}', '').trim();
                const pairs = cleanValStr.split(',').map(s => s.trim());
                const obj = {};
                for (const pair of pairs) {
                    let [k, v] = pair.split(':');
                    if (!k || !v) continue;
                    k = k.trim(); v = v.trim();
                    if (v.startsWith('"') && v.endsWith('"')) v = v.slice(1, -1);
                    obj[k] = v;
                }
                result.boards[currentBoard].pins[sec][key.trim()] = obj;
            }
        }
    }
    return result;
}

/* --------------------------------------------------------------------------
 * GET /api/boards
 * Returns parsed boards.yaml as JSON for the UI to build its pin picker.
 * ----------------------------------------------------------------------- */
app.get('/api/boards', (req, res) => {
    try {
        const yaml    = fs.readFileSync(BOARDS_YAML, 'utf8');
        const parsed  = parseSimpleYaml(yaml);
        res.json(parsed.boards);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

/* --------------------------------------------------------------------------
 * POST /api/schematic/import
 * Previews schematic import
 * ----------------------------------------------------------------------- */
app.post('/api/schematic/import', (req, res) => {
    try {
        const { filename, content } = req.body;
        if (!filename || !content) {
            return res.status(400).json({ error: 'filename and content required' });
        }

        const yaml    = fs.readFileSync(BOARDS_YAML, 'utf8');
        const parsed  = parseSimpleYaml(yaml);
        const boardsDict = parsed.boards;

        // Load the importer after this module has initialized.  The resource
        // mapper reuses projectHardware from this module; eager loading here
        // creates a CommonJS cycle and leaves that function undefined.
        const { importSchematic } = require('../schematic');
        const result = importSchematic(filename, content, { boardsDict });
        res.json(result);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

/* --------------------------------------------------------------------------
 * Generic project hardware configuration.  Board capabilities remain in
 * boards.yaml; this small project file only stores the user's assignments and
 * resource settings.  It deliberately contains no target-specific rules.
 * ----------------------------------------------------------------------- */
function defaultResourceConfig(board) {
    const pins = board.pins || {};
    const resources = {};
    for (const type of ['spi', 'i2c', 'uart']) {
        for (const [instance, signals] of Object.entries(pins[type] || {})) {
            resources[`${type}.${instance}`] = {
                id: `${type}.${instance}`, type, instance, available: true,
                assignments: Object.entries(signals).map(([role, pin]) => ({ role, pin })), configuration: {}
            };
        }
    }
    for (const type of ['pwm', 'adc']) {
        for (const pin of pins[type] || []) {
            resources[`${type}.${pin}`] = { id: `${type}.${pin}`, type, instance: pin, available: true,
                assignments: [{ role: type === 'pwm' ? 'output' : 'input', pin }], configuration: {} };
        }
    }
    for (const pin of pins.gpio || []) {
        resources[`gpio.${pin}`] = { id: `gpio.${pin}`, type: 'gpio', instance: pin, available: true,
            assignments: [{ role: 'gpio', pin }], configuration: {} };
    }
    for (const accelerator of board.accelerators || []) {
        const name = String(accelerator).toLowerCase();
        resources[`accelerator.${name}`] = {
            id: `accelerator.${name}`, type: 'accelerator', instance: name,
            available: true, assignments: [], configuration: {}
        };
    }
    return resources;
}

function normalizeDeviceProfiles(board, devices = []) {
    if (devices == null) return [];
    if (!Array.isArray(devices)) throw new Error('Hardware devices must be an array.');
    const parsed = parseSimpleYaml(fs.readFileSync(BOARDS_YAML, 'utf8'));
    const resources = defaultResourceConfig(parsed.boards[board]);
    const ids = new Set();
    return devices.map((device, index) => {
        if (!device || typeof device !== 'object' || !/^[A-Za-z][A-Za-z0-9_-]*$/.test(String(device.id || ''))) {
            throw new Error(`Device ${index + 1} has an invalid id.`);
        }
        const id = String(device.id);
        if (ids.has(id)) throw new Error(`Device '${id}' is duplicated.`);
        ids.add(id);
        if (!Array.isArray(device.connections)) throw new Error(`Device '${id}' connections must be an array.`);
        const names = new Set();
        const connections = device.connections.map((connection, connectionIndex) => {
            if (!connection || typeof connection !== 'object' || !/^[A-Za-z][A-Za-z0-9_-]*$/.test(String(connection.name || ''))) {
                throw new Error(`Device '${id}' connection ${connectionIndex + 1} has an invalid name.`);
            }
            const name = String(connection.name);
            if (names.has(name)) throw new Error(`Device '${id}' connection '${name}' is duplicated.`);
            names.add(name);
            const resource = String(connection.resource || '');
            if (!resources[resource]) throw new Error(`Device '${id}' references unknown hardware resource '${resource}'.`);
            const valid = resources[resource].assignments.some(signal => signal.pin === connection.pin);
            if (!valid) throw new Error(`Device '${id}' connection '${name}' uses invalid pin '${connection.pin}' for '${resource}'.`);
            return { name, resource, pin: String(connection.pin), ...(connection.role ? { role: String(connection.role) } : {}) };
        });
        return { id, name: String(device.name || id), profile: String(device.profile || ''), connections };
    });
}

function readHardwareConfig() {
    try {
        const stored = JSON.parse(fs.readFileSync(HARDWARE_CONFIG, 'utf8'));
        if (!stored.board || !Array.isArray(stored.assignments)) return stored;
        const configurations = Object.fromEntries(Object.entries(stored.resources || {})
            .map(([id, resource]) => [id, resource.configuration || {}]));
        const migrated = projectHardware(stored.board, stored.assignments, configurations, stored.devices);
        if (JSON.stringify(stored) !== JSON.stringify(migrated)) writeHardwareConfig(migrated);
        return migrated;
    }
    catch (_) { return { version: 1, board: null, resources: {}, assignments: [] }; }
}

function writeHardwareConfig(config) {
    fs.mkdirSync(path.dirname(HARDWARE_CONFIG), { recursive: true });
    fs.writeFileSync(HARDWARE_CONFIG, JSON.stringify(config, null, 2) + '\n', 'utf8');
}

function projectHardware(boardKey, assignments, configurations = {}, devices = []) {
    const parsed = parseSimpleYaml(fs.readFileSync(BOARDS_YAML, 'utf8'));
    const board = parsed.boards[boardKey];
    if (!board) throw new Error(`Unknown board '${boardKey}'.`);
    const resources = defaultResourceConfig(board);
    const seenPins = new Set();
    const normalized = assignments.map((assignment, index) => {
        if (!assignment || typeof assignment.pin !== 'string' || typeof assignment.role !== 'string') {
            throw new Error(`Assignment ${index + 1} is incomplete.`);
        }
        if (seenPins.has(assignment.pin)) throw new Error(`Pin ${assignment.pin} is assigned more than once.`);
        seenPins.add(assignment.pin);
        let resourceId = typeof assignment.resource === 'string' && assignment.resource
            ? assignment.resource : (assignment.peripheral || 'gpio');
        if (resourceId === 'gpio') {
            resourceId = `gpio.${assignment.pin}`;
        } else if (!resources[resourceId]) {
            const matchingResourceId = Object.keys(resources).find(id => {
                const res = resources[id];
                if (res.type === resourceId || id.startsWith(resourceId + '.')) {
                    return (res.assignments || []).some(s => s.pin === assignment.pin);
                }
                return false;
            });
            if (matchingResourceId) {
                resourceId = matchingResourceId;
            }
        }
        if (!resources[resourceId]) throw new Error(`Unknown hardware resource '${resourceId}'.`);
        const resource = resources[resourceId];
        if (resource.type === 'accelerator') {
            throw new Error(`Hardware resource '${resourceId}' is an accelerator and cannot have pin assignments.`);
        }
        const legacyMap = LEGACY_SIGNAL_ROLES[resource.type] || {};
        const role = legacyMap[assignment.role] || assignment.role;
        if (!RESOURCE_SIGNAL_ROLES[resource.type] || !RESOURCE_SIGNAL_ROLES[resource.type].has(role)) {
            throw new Error(`Signal '${assignment.role}' is not valid for hardware resource '${resourceId}'.`);
        }
        const validSignalPin = (resource.assignments || []).some(signal => signal.role === role && signal.pin === assignment.pin);
        if (!validSignalPin) throw new Error(`Pin ${assignment.pin} is not valid for signal '${role}' on hardware resource '${resourceId}'.`);
        return { node: String(assignment.node || ''), role, pin: assignment.pin, resource: resourceId };
    });
    for (const [id, configuration] of Object.entries(configurations || {})) {
        if (!resources[id]) throw new Error(`Unknown hardware resource '${id}'.`);
        if (!configuration || typeof configuration !== 'object' || Array.isArray(configuration)) throw new Error(`Invalid configuration for '${id}'.`);
        resources[id].configuration = configuration;
    }
    // Keep the legacy function extraction used by scratch/test_hardware_model.js
    // independent when no profile layer is supplied.
    const normalizedDevices = devices && devices.length ? normalizeDeviceProfiles(boardKey, devices) : [];
    return { version: 1, board: boardKey, resources, assignments: normalized, devices: normalizedDevices };
}

app.get('/api/hardware', (_req, res) => res.json(readHardwareConfig()));

app.post('/api/hardware', (req, res) => {
    try {
        const { board, assignments, configurations, devices } = req.body || {};
        if (!board || !Array.isArray(assignments)) return res.status(400).json({ error: 'Missing board or assignments.' });
        const hardware = projectHardware(board, assignments, configurations, devices);
        writeHardwareConfig(hardware);
        res.json({ success: true, hardware });
    } catch (err) { res.status(400).json({ error: err.message }); }
});

function validateGraphHardwareResources(graph) {
    const hardware = readHardwareConfig();
    const graphBoard = graph.metadata && graph.metadata.targetBoard;
    if (graphBoard && graphBoard !== hardware.board) {
        throw new Error(`Graph target '${graphBoard}' is stale; Hardware Setup target is '${hardware.board || 'none'}'. Review hardware resource bindings before building.`);
    }
    const configured = new Set((hardware.assignments || []).map(assignment => assignment.resource));
    for (const [id, resource] of Object.entries(hardware.resources || {})) {
        if (resource.type === 'accelerator' || (resource.configuration && Object.keys(resource.configuration).length > 0)) {
            configured.add(id);
        }
    }
    for (const node of graph.nodes || []) {
        if (!node || !['SensorInput', 'ActuatorOutput', 'GPIOInput', 'ADCInput', 'PWMOutput', 'CordicOp'].includes(node.type)) continue;
        const resourceId = node.params && node.params.hardwareResource;
        if (!resourceId) continue;
        const expectedType = { SensorInput: null, ActuatorOutput: null, GPIOInput: 'gpio', ADCInput: 'adc', PWMOutput: 'pwm', CordicOp: 'accelerator' }[node.type];
        if (expectedType && !resourceId.startsWith(`${expectedType}.`)) {
            throw new Error(`Node '${node.id}' requires a ${expectedType} hardware resource, got '${resourceId}'.`);
        }
        if (!hardware.resources || !hardware.resources[resourceId] || !configured.has(resourceId)) {
            throw new Error(`Node '${node.id}' references hardware resource '${resourceId}', which is not configured in Hardware Setup.`);
        }
    }
}

/* --------------------------------------------------------------------------
 * POST /api/generate
 * Body: { board: "thejas32", assignments: [ { node: "SensorInput[0]", peripheral: "spi0", pin: "SPI0MOSI", role: "mosi" }, ... ] }
 *
 * Writes an augmented boards.yaml (with assignments embedded as comments),
 * runs gen_board_config.js, and returns the generated header text plus a
 * machine-readable assignment block for MBD-T1b wiring.
 * ----------------------------------------------------------------------- */
app.post('/api/generate', (req, res) => {
    try {
        const { board, assignments, configurations, devices } = req.body;
        if (!board || !Array.isArray(assignments)) {
            return res.status(400).json({ error: 'Missing board or assignments.' });
        }

        const hardware = projectHardware(board, assignments, configurations, devices);
        writeHardwareConfig(hardware);

        /* Run the existing codegen script */
        const cmd = `node "${CODEGEN_JS}" "${board}" "${BOARDS_YAML}" "${CODEGEN_OUT}"`;
        execSync(cmd, { stdio: 'pipe' });

        /* Read the generated base header */
        const headerPath  = path.join(CODEGEN_OUT, 'hyp_board_config.h');
        let   headerText  = fs.readFileSync(headerPath, 'utf8');

        /* Inject the MBD pin assignments as #defines — MBD-T1b wiring */
        if (assignments.length > 0) {
            const pinDefines = [
                '',
                '/* MBD Pin Assignments — generated by pin_config UI (MBD-T1b) */'
            ];
            const definedPeripherals = new Set();
            for (const a of hardware.assignments) {
                const macroBase = a.node.replace(/[^A-Za-z0-9_]/g, '_').replace(/_+/g, '_').replace(/_$/, '').toUpperCase();
                pinDefines.push(`#define HYP_PIN_${macroBase}_${a.role.toUpperCase()} "${a.pin}"  /* ${a.node} → ${a.resource}.${a.role} */`);
                if (!definedPeripherals.has(`${macroBase}:${a.resource}`)) {
                    pinDefines.push(`#define HYP_PERIPH_${macroBase} "${a.resource}"`);
                    definedPeripherals.add(`${macroBase}:${a.resource}`);
                }
            }
            headerText = headerText.replace(
                '#endif /* HYP_BOARD_CONFIG_H */',
                pinDefines.join('\n') + '\n\n#endif /* HYP_BOARD_CONFIG_H */'
            );
            fs.writeFileSync(headerPath, headerText, 'utf8');
        }

        /* Resource identities and configuration are emitted once here, not
           recreated by graph nodes.  Values remain plain generic project
           settings; the ESP32 backend interprets the selected board's pins. */
        const resourceDefines = ['','/* Hardware Setup resources — generated from project hardware.json */'];
        for (const resource of Object.values(hardware.resources)) {
            const macro = resource.id.toUpperCase().replace(/[^A-Z0-9_]/g, '_');
            const assigned = hardware.assignments.filter(item => item.resource === resource.id);
            if (assigned.length > 0) resourceDefines.push(`#define HYP_RESOURCE_${macro} 1`);
            for (const [key, value] of Object.entries(resource.configuration || {})) {
                const keyMacro = key.replace(/([a-z])([A-Z])/g, '$1_$2').toUpperCase().replace(/[^A-Z0-9_]/g, '_');
                resourceDefines.push(`#define HYP_RESOURCE_${macro}_${keyMacro} ${typeof value === 'number' ? value : JSON.stringify(String(value))}`);
            }
        }
        headerText = headerText.replace('#endif /* HYP_BOARD_CONFIG_H */', resourceDefines.join('\n') + '\n\n#endif /* HYP_BOARD_CONFIG_H */');
        fs.writeFileSync(headerPath, headerText, 'utf8');

        res.json({ success: true, header: headerText, assignments: hardware.assignments, hardware });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

/* --------------------------------------------------------------------------
 * POST /api/build
 * Body: a hypraccel.mbd.graph object from graph_editor.html.
 * The existing graph_to_c.js remains the source of truth for validation and
 * generated C; this endpoint only supplies its temporary JSON/C file paths.
 * ----------------------------------------------------------------------- */
app.post('/api/build', (req, res) => {
    const graph = req.body;
    if (!graph || typeof graph !== 'object' || !Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
        return res.status(400).json({ error: 'Body must be a graph with nodes and edges arrays.' });
    }

    let tempDir;
    try {
        validateGraphHardwareResources(graph);
        tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-mbd-build-'));
        const graphPath = path.join(tempDir, 'graph.json');
        const outputPath = path.join(tempDir, 'graph.c');
        fs.writeFileSync(graphPath, JSON.stringify(graph, null, 2), 'utf8');
        execFileSync(process.execPath, [GRAPH_CODEGEN, graphPath, outputPath], { encoding: 'utf8', stdio: 'pipe' });
        res.json({ success: true, graph, source: fs.readFileSync(outputPath, 'utf8') });
    } catch (err) {
        const detail = err.stderr ? String(err.stderr).trim() : err.message;
        res.status(422).json({ error: detail || 'Graph code generation failed.' });
    } finally {
        if (tempDir) {
            for (const file of ['graph.json', 'graph.c']) {
                try { fs.unlinkSync(path.join(tempDir, file)); } catch (_) { /* best-effort temp cleanup */ }
            }
            try { fs.rmdirSync(tempDir); } catch (_) { /* best-effort temp cleanup */ }
        }
    }
});

/* --------------------------------------------------------------------------
 * MBD-T9 ESP32 materialization and commands
 * ----------------------------------------------------------------------- */
function commandResult(command, args, options = {}) {
    const result = spawnSync(command, args, {
        cwd: options.cwd || REPO_ROOT,
        encoding: 'utf8',
        timeout: options.timeout || 180000
    });
    const output = [result.stdout, result.stderr].filter(Boolean).join('\n').trim();
    if (result.error) return { ok: false, output: `${output}\n${result.error.message}`.trim() };
    return { ok: result.status === 0, output: output || `(command exited ${result.status})` };
}

function graphVerificationMarkers(graph) {
    if (!graph || typeof graph.id !== 'string') {
        throw new Error('Graph id is required for serial verification.');
    }
    const publish = graph.nodes.find(node => node && node.type === 'Publish');
    return [
        'HYPRACCEL_MBD_T9_READY',
        `HYPRACCEL_GRAPH_ID=${graph.id}`,
        publish && publish.params && typeof publish.params.topic === 'string'
            ? `HYP_PUBLISH topic=${publish.params.topic}` : null
    ].filter(Boolean);
}

function platformioCommand() {
    if (process.env.HYPRACCEL_PLATFORMIO) return process.env.HYPRACCEL_PLATFORMIO;
    const local = path.join(REPO_ROOT, '.venv-platformio/bin/pio');
    return fs.existsSync(local) ? local : 'pio';
}

function serialCandidates() {
    const byIdCandidates = [];
    const byId = '/dev/serial/by-id';
    try {
        for (const entry of fs.readdirSync(byId)) byIdCandidates.push(path.join(byId, entry));
    } catch (_) { /* absent on machines without serial hardware */ }
    /* Stable /dev/serial/by-id names and /dev/ttyUSB0 are aliases; prefer the
       stable names so one connected board does not look like two choices. */
    if (byIdCandidates.length > 0) return [...new Set(byIdCandidates)];
    const candidates = [];
    for (const prefix of ['/dev/ttyUSB', '/dev/ttyACM']) {
        for (let index = 0; index < 16; index++) {
            const candidate = `${prefix}${index}`;
            if (fs.existsSync(candidate)) candidates.push(candidate);
        }
    }
    return [...new Set(candidates)];
}

function selectedSerialPort(requestedPort) {
    if (requestedPort) return { port: requestedPort };
    if (ESP32_PORT) return { port: ESP32_PORT };
    const candidates = serialCandidates();
    if (candidates.length === 1) return { port: candidates[0] };
    if (candidates.length === 0) return { error: 'No ESP32 serial port found. Connect the board or set HYPRACCEL_ESP32_PORT.' };
    return { error: `Multiple serial ports found; set HYPRACCEL_ESP32_PORT or select one: ${candidates.join(', ')}` };
}

function graphStepSymbol(graph) {
    if (!graph || typeof graph.id !== 'string' || !/^[A-Za-z][A-Za-z0-9_-]*$/.test(graph.id)) {
        throw new Error('Graph id is not valid for an ESP32 build.');
    }
    return `hyp_graph_${graph.id.replace(/-/g, '_')}_step`;
}

function materializeEsp32(graph) {
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-esp32-'));
    try {
        validateGraphHardwareResources(graph);
        const graphPath = path.join(tempDir, 'graph.json');
        const graphSource = path.join(tempDir, 'graph.c');
        fs.writeFileSync(graphPath, JSON.stringify(graph, null, 2), 'utf8');
        execFileSync(process.execPath, [GRAPH_CODEGEN, graphPath, graphSource], { encoding: 'utf8', stdio: 'pipe' });
        const source = fs.readFileSync(graphSource, 'utf8');
        const step = graphStepSymbol(graph);

        fs.rmSync(ESP32_GENERATED, { recursive: true, force: true });
        fs.mkdirSync(ESP32_GENERATED, { recursive: true });
        fs.writeFileSync(path.join(ESP32_GENERATED, 'graph.c'), source, 'utf8');
        for (const file of ['hyp_esp32.c', 'hyp_router.c', 'hyp_cordic_ref.c', 'hyp_cordic_ref.h', 'hyp_esp32_hw.cpp']) {
            fs.copyFileSync(path.join(REPO_ROOT, 'sdk/src', file), path.join(ESP32_GENERATED, file));
        }
        fs.copyFileSync(path.join(REPO_ROOT, 'sdk/include/hyprccel.h'), path.join(ESP32_GENERATED, 'hyprccel.h'));
        fs.copyFileSync(path.join(REPO_ROOT, 'sdk/include/hyp_esp32_hw.h'), path.join(ESP32_GENERATED, 'hyp_esp32_hw.h'));
        fs.copyFileSync(path.join(REPO_ROOT, 'boards/codegen/hyp_board_config.h'), path.join(ESP32_GENERATED, 'hyp_board_config.h'));
        fs.writeFileSync(path.join(ESP32_GENERATED, 'main.cpp'), `/* Generated MBD-T9 runtime wrapper; graph.c is the application logic. */
#include <Arduino.h>
#include "hyprccel.h"
#include "hyp_esp32_hw.h"

extern "C" int hyp_graph_init(void);
extern "C" void ${step}(float angle_rad);

extern "C" void hyp_esp32_publish(const char *topic, const void *data, uint32_t size)
{
    Serial.print("HYP_PUBLISH topic=");
    Serial.print(topic ? topic : "(null)");
    if (data && size == sizeof(float)) {
        Serial.print(" value=");
        Serial.print(*static_cast<const float *>(data), 6);
    }
    Serial.print(" bytes=");
    Serial.println(size);
}

void setup()
{
    Serial.begin(115200);
    delay(250);
    Serial.println("HYPRACCEL_MBD_T9_READY");
    Serial.print("HYPRACCEL_GRAPH_ID=");
    Serial.println("${graph.id}");

    int hw_status = hyp_esp32_hw_init();
    if (hw_status == 0) {
        Serial.println("HYPRACCEL_HW_INIT_OK");
        if (hyp_graph_init() == 0) {
            Serial.println("HYPRACCEL_INIT_OK");
        } else {
            Serial.println("HYPRACCEL_INIT_FAILED");
        }
    } else {
        Serial.print("[ERROR] ESP32 hardware initialization failed with code: ");
        Serial.println(hw_status);
        Serial.println("HYPRACCEL_INIT_FAILED");
    }
}

void loop()
{
    ${step}(1.0f);
    delay(1000);
}
`, 'utf8');
        return { source, generatedDir: ESP32_GENERATED };
    } finally {
        fs.rmSync(tempDir, { recursive: true, force: true });
    }
}

function validGraphBody(req, res) {
    const graph = req.body && req.body.graph ? req.body.graph : req.body;
    if (!graph || typeof graph !== 'object' || !Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
        res.status(400).json({ error: 'Body must be a graph with nodes and edges arrays.' });
        return null;
    }
    return graph;
}

app.get('/api/esp32/ports', (_req, res) => {
    res.json({ configuredPort: ESP32_PORT || null, candidates: serialCandidates() });
});

app.post('/api/compile', (req, res) => {
    const graph = validGraphBody(req, res);
    if (!graph) return;
    try {
        const generated = materializeEsp32(graph);
        const compile = commandResult(platformioCommand(), ['run', '--project-dir', ESP32_PROJECT]);
        const status = compile.ok ? 200 : 422;
        fs.writeFileSync(path.join(REPO_ROOT, '.hypraccel', 'build.log'), compile.output, 'utf8');
        res.status(status).json({ success: compile.ok, stage: 'Compiling', source: generated.source, log: compile.output });
    } catch (err) {
        const detail = err.stderr ? String(err.stderr).trim() : err.message;
        res.status(422).json({ error: detail || 'ESP32 source preparation failed.' });
    }
});

app.post('/api/flash', (req, res) => {
    const graph = validGraphBody(req, res);
    if (!graph) return;
    try {
        const generated = materializeEsp32(graph);
        const compile = commandResult(platformioCommand(), ['run', '--project-dir', ESP32_PROJECT]);
        if (!compile.ok) {
            fs.writeFileSync(path.join(REPO_ROOT, '.hypraccel', 'build.log'), compile.output, 'utf8');
            return res.status(422).json({ success: false, stage: 'Compiling', source: generated.source, log: compile.output });
        }
        const selection = selectedSerialPort(req.body.port);
        if (selection.error) {
            fs.writeFileSync(path.join(REPO_ROOT, '.hypraccel', 'build.log'), `${compile.output}\n${selection.error}`, 'utf8');
            return res.status(422).json({ success: false, stage: 'Flashing', source: generated.source, log: `${compile.output}\n${selection.error}` });
        }
        const flash = commandResult(platformioCommand(), ['run', '--project-dir', ESP32_PROJECT, '--target', 'upload', '--upload-port', selection.port]);
        const logContent = `${compile.output}\n\n${flash.output}`;
        fs.writeFileSync(path.join(REPO_ROOT, '.hypraccel', 'build.log'), logContent, 'utf8');
        res.status(flash.ok ? 200 : 422).json({ success: flash.ok, stage: flash.ok ? 'Flashed' : 'Flashing', source: generated.source, port: selection.port, log: logContent });
    } catch (err) {
        const detail = err.stderr ? String(err.stderr).trim() : err.message;
        res.status(422).json({ error: detail || 'ESP32 flash preparation failed.' });
    }
});

/* Read the board's generated runtime banner and first publish event.  The
 * monitor intentionally times out; seeing all markers is the success signal. */
app.post('/api/verify', (req, res) => {
    const graph = validGraphBody(req, res);
    if (!graph) return;
    try {
        const selection = selectedSerialPort(req.body.port);
        if (selection.error) {
            fs.writeFileSync(path.join(REPO_ROOT, '.hypraccel', 'build.log'), selection.error, 'utf8');
            return res.status(422).json({ success: false, stage: 'Verifying', log: selection.error });
        }
        const markers = graphVerificationMarkers(graph);
        const monitor = commandResult('timeout', ['10s', platformioCommand(), 'device', 'monitor', '--port', selection.port, '--baud', '115200'], { timeout: 15000 });
        const missing = markers.filter(marker => !monitor.output.includes(marker));
        const verified = missing.length === 0;
        fs.writeFileSync(path.join(REPO_ROOT, '.hypraccel', 'build.log'), monitor.output, 'utf8');
        res.status(verified ? 200 : 422).json({
            success: verified,
            stage: 'Verifying',
            port: selection.port,
            markers,
            missing,
            log: monitor.output
        });
    } catch (err) {
        res.status(422).json({ error: err.message || 'ESP32 serial verification failed.' });
    }
});

/* --------------------------------------------------------------------------
 * Project Workspace Endpoints
 * ----------------------------------------------------------------------- */
app.post('/api/project/generate', (req, res) => {
    const graph = req.body;
    if (!graph || !graph.id) return res.status(400).json({ error: 'Valid graph required' });
    try {
        const hardware = readHardwareConfig();
        const projectMetadata = {
            id: graph.id,
            name: graph.name || graph.id,
            board: hardware.board,
            timestamp: Date.now()
        };
        fs.mkdirSync(path.join(REPO_ROOT, '.hypraccel'), { recursive: true });
        fs.writeFileSync(path.join(REPO_ROOT, '.hypraccel', 'project.json'), JSON.stringify(projectMetadata, null, 2), 'utf8');
        fs.writeFileSync(path.join(REPO_ROOT, '.hypraccel', 'graph.json'), JSON.stringify(graph, null, 2), 'utf8');

        // Materialize the deployable source tree
        materializeEsp32(graph);

        res.json({ success: true, project: projectMetadata });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/project', (req, res) => {
    try {
        let project = {};
        try { project = JSON.parse(fs.readFileSync(path.join(REPO_ROOT, '.hypraccel', 'project.json'), 'utf8')); } catch (_) {}
        let graph = null;
        try { graph = JSON.parse(fs.readFileSync(path.join(REPO_ROOT, '.hypraccel', 'graph.json'), 'utf8')); } catch (_) {}
        res.json({ ...project, graph });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/project/file', (req, res) => {
    try {
        const relPath = req.query.path;
        if (!relPath || relPath.includes('..')) return res.status(400).json({ error: 'Invalid path' });
        const absPath = path.join(REPO_ROOT, relPath);
        if (!fs.existsSync(absPath)) return res.status(404).json({ error: 'File not found' });
        const content = fs.readFileSync(absPath, 'utf8');
        res.json({ content });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

if (require.main === module) {
    app.listen(PORT, HOST, () => {
        console.log(`HyprAccel MBD Editor  →  http://localhost:${PORT}`);
        console.log(`Boards YAML           →  ${BOARDS_YAML}`);
        console.log(`Codegen output        →  ${CODEGEN_OUT}/hyp_board_config.h`);
    });
}

module.exports = {
    app,
    parseSimpleYaml,
    defaultResourceConfig,
    projectHardware,
    validateGraphHardwareResources,
    readHardwareConfig,
    writeHardwareConfig
};
