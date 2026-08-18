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
const HYPRACCEL_DIR = path.join(REPO_ROOT, '.hypraccel');
/* The environment overrides are intentionally test-only seams.  Production
 * always uses the repository-local store, never a copy under ~/ or /tmp. */
const PROJECTS_ROOT = process.env.HYPRACCEL_PROJECTS_ROOT || path.join(HYPRACCEL_DIR, 'projects');
const HARDWARE_CONFIG = process.env.HYPRACCEL_HARDWARE_CONFIG || path.join(HYPRACCEL_DIR, 'hardware.json');
const PROJECT_ID = /^[A-Za-z][A-Za-z0-9_-]{0,63}$/;
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

function assertProjectId(id) {
    if (typeof id !== 'string' || !PROJECT_ID.test(id)) {
        throw new Error('Project id must start with a letter and contain only letters, numbers, hyphens, or underscores.');
    }
    return id;
}

function projectPaths(id) {
    id = assertProjectId(id);
    const projectDir = path.resolve(PROJECTS_ROOT, id);
    const root = path.resolve(PROJECTS_ROOT) + path.sep;
    if (!projectDir.startsWith(root)) throw new Error('Invalid project path.');
    return {
        projectDir,
        manifest: path.join(projectDir, 'project.json'),
        hardware: path.join(projectDir, 'hardware', 'hardware.json'),
        graph: path.join(projectDir, 'graph', 'graph.json'),
        generated: path.join(projectDir, 'generated'),
        platformio: path.join(projectDir, 'platformio.ini'),
        buildDir: path.join(projectDir, 'build'),
        buildLog: path.join(projectDir, 'build', 'build.log')
    };
}

function readJson(file, label) {
    try {
        return JSON.parse(fs.readFileSync(file, 'utf8'));
    } catch (err) {
        if (err.code === 'ENOENT') return null;
        throw new Error(`Could not read ${label}: ${err.message}`);
    }
}

function writeJson(file, value) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n', 'utf8');
}

function requestedProjectId(req) {
    const candidate = req.query && (req.query.projectId || req.query.project)
        || req.body && req.body.projectId;
    return candidate == null || candidate === '' ? null : assertProjectId(candidate);
}

function hardwareConfigPath(projectId) {
    return projectId ? projectPaths(projectId).hardware : HARDWARE_CONFIG;
}

function readHardwareConfig(projectId = null) {
    try {
        const configPath = projectId ? hardwareConfigPath(projectId) : HARDWARE_CONFIG;
        const stored = JSON.parse(fs.readFileSync(configPath, 'utf8'));
        if (!stored.board || !Array.isArray(stored.assignments)) return stored;
        // Build the canonical resource set for the stored board first to know valid resource IDs
        const parsed = parseSimpleYaml(fs.readFileSync(BOARDS_YAML, 'utf8'));
        const board = parsed.boards[stored.board];
        if (!board) return stored; // Unknown board, skip migration
        const canonicalResources = defaultResourceConfig(board);
        // Filter configurations to only include resources that exist for this board
        const configurations = Object.fromEntries(
            Object.entries(stored.resources || {})
                .filter(([id]) => canonicalResources[id]) // Only keep configs for valid resources
                .map(([id, resource]) => [id, resource.configuration || {}])
        );
        const migrated = projectHardware(stored.board, stored.assignments, configurations, stored.devices);
        if (JSON.stringify(stored) !== JSON.stringify(migrated)) writeHardwareConfig(migrated, projectId);
        return migrated;
    }
    catch (_) { return { version: 1, board: null, resources: {}, assignments: [] }; }
}

function writeHardwareConfig(config, projectId = null) {
    writeJson(hardwareConfigPath(projectId), config);
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

/* --------------------------------------------------------------------------
 * Persistent project store
 *
 * project.json is a manifest, not another copy of hardware or graph state.
 * The board lives only in hardware/hardware.json and the graph id lives only
 * in graph/graph.json.  API responses derive those convenient summary fields.
 * ----------------------------------------------------------------------- */
function canonicalHardware(payload) {
    if (!payload || typeof payload !== 'object' || !payload.board) {
        throw new Error('Hardware state must include a board.');
    }
    const configurations = payload.configurations || Object.fromEntries(
        Object.entries(payload.resources || {}).map(([id, resource]) => [id, resource.configuration || {}])
    );
    return projectHardware(payload.board, payload.assignments || [], configurations, payload.devices);
}

function assertGraphDocument(graph) {
    if (!graph || typeof graph !== 'object' || graph.format !== 'hypraccel.mbd.graph' || graph.version !== 1 ||
        typeof graph.id !== 'string' || !PROJECT_ID.test(graph.id) || !Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
        throw new Error('Graph must be a hypraccel.mbd.graph version 1 document with a valid id, nodes, and edges.');
    }
    return graph;
}

function readProjectManifest(id) {
    const paths = projectPaths(id);
    const manifest = readJson(paths.manifest, 'project manifest');
    if (!manifest) {
        const err = new Error(`Project '${id}' was not found.`);
        err.code = 'ENOENT';
        throw err;
    }
    if (manifest.id !== id || manifest.format !== 'hypraccel.project' || manifest.version !== 1) {
        throw new Error(`Project '${id}' has an invalid manifest.`);
    }
    return manifest;
}

function projectResponse(id, includeComponents = true) {
    const manifest = readProjectManifest(id);
    const paths = projectPaths(id);
    const hardware = readJson(paths.hardware, 'project hardware');
    const graph = readJson(paths.graph, 'project graph');
    const response = {
        ...manifest,
        board: hardware && hardware.board || null,
        graphId: graph && graph.id || null,
        components: { hardware: Boolean(hardware), graph: Boolean(graph) }
    };
    if (includeComponents) {
        response.hardware = hardware;
        response.graph = graph;
    }
    return response;
}

function createProject(input) {
    const id = assertProjectId(input && input.id);
    if (!input || typeof input.name !== 'string' || !input.name.trim() || input.name.trim().length > 120) {
        throw new Error('Project name must be a non-empty string no longer than 120 characters.');
    }
    const paths = projectPaths(id);
    if (fs.existsSync(paths.projectDir)) throw new Error(`Project '${id}' already exists.`);
    const now = new Date().toISOString();
    const manifest = {
        format: 'hypraccel.project', version: 1, id, name: input.name.trim(),
        hardware: { path: 'hardware/hardware.json' },
        graph: { path: 'graph/graph.json' },
        createdAt: now, updatedAt: now
    };
    fs.mkdirSync(paths.projectDir, { recursive: true });
    try {
        writeJson(paths.manifest, manifest);
        if (input.hardware != null) writeHardwareConfig(canonicalHardware(input.hardware), id);
        if (input.graph != null) writeJson(paths.graph, assertGraphDocument(input.graph));
        return projectResponse(id);
    } catch (err) {
        fs.rmSync(paths.projectDir, { recursive: true, force: true });
        throw err;
    }
}

function updateProject(id, input) {
    const paths = projectPaths(id);
    const manifest = readProjectManifest(id);
    if (!input || typeof input !== 'object') throw new Error('Project update body must be an object.');
    if (Object.prototype.hasOwnProperty.call(input, 'id') && input.id !== id) {
        throw new Error('Project id cannot be changed.');
    }
    if (Object.prototype.hasOwnProperty.call(input, 'name')) {
        if (typeof input.name !== 'string' || !input.name.trim() || input.name.trim().length > 120) {
            throw new Error('Project name must be a non-empty string no longer than 120 characters.');
        }
        manifest.name = input.name.trim();
    }
    if (input.hardware != null) writeHardwareConfig(canonicalHardware(input.hardware), id);
    if (input.graph != null) writeJson(paths.graph, assertGraphDocument(input.graph));
    manifest.updatedAt = new Date().toISOString();
    writeJson(paths.manifest, manifest);
    return projectResponse(id);
}

function writeProjectGraph(id, graph) {
    readProjectManifest(id);
    const paths = projectPaths(id);
    writeJson(paths.graph, assertGraphDocument(graph));
    const manifest = readProjectManifest(id);
    manifest.updatedAt = new Date().toISOString();
    writeJson(paths.manifest, manifest);
    return projectResponse(id);
}

function writeProjectBuildLog(id, content) {
    if (!id) return;
    readProjectManifest(id);
    const paths = projectPaths(id);
    fs.mkdirSync(paths.buildDir, { recursive: true });
    fs.writeFileSync(paths.buildLog, content, 'utf8');
}

function boardDescriptor(boardKey) {
    const parsed = parseSimpleYaml(fs.readFileSync(BOARDS_YAML, 'utf8'));
    const board = parsed.boards[boardKey];
    if (!board) throw new Error(`Unknown board '${boardKey}'.`);
    return board;
}

function platformioBoardId(boardKey) {
    const descriptor = boardDescriptor(boardKey);
    // Board descriptors may expose a user-facing name followed by the
    // PlatformIO id, e.g. "ESP32 DevKit V1 / esp32dev".
    const declared = typeof descriptor.board === 'string' ? descriptor.board : '';
    const candidate = declared.split('/').pop().trim();
    return candidate || boardKey;
}

function platformioEnvironment(boardKey) {
    return platformioBoardId(boardKey).replace(/[^A-Za-z0-9_-]/g, '_');
}

function projectPlatformioIni(boardKey) {
    const environment = platformioEnvironment(boardKey);
    const board = platformioBoardId(boardKey);
    return `; Generated by HyprAccel from the project's canonical hardware.json\n` +
        `[platformio]\n` +
        `src_dir = generated\n\n` +
        `[env:${environment}]\n` +
        `platform = espressif32\n` +
        `board = ${board}\n` +
        `framework = arduino\n` +
        `monitor_speed = 115200\n` +
        `upload_speed = 921600\n` +
        `build_flags =\n` +
        `    -I generated\n` +
        // The current SDK header/source pair contains an existing C++ linkage
        // mismatch for parse_resource_id; keep project builds compatible while
        // leaving the SDK implementation untouched.
        `    -fpermissive\n`;
}

function injectHardwareHeader(headerPath, hardware) {
    let headerText = fs.readFileSync(headerPath, 'utf8');
    const pinDefines = ['', '/* MBD Pin Assignments — generated from project hardware.json */'];
    const definedPeripherals = new Set();
    for (const assignment of hardware.assignments || []) {
        const macroBase = assignment.node.replace(/[^A-Za-z0-9_]/g, '_').replace(/_+/g, '_').replace(/_$/, '').toUpperCase();
        pinDefines.push(`#define HYP_PIN_${macroBase}_${assignment.role.toUpperCase()} "${assignment.pin}"  /* ${assignment.node} → ${assignment.resource}.${assignment.role} */`);
        if (!definedPeripherals.has(`${macroBase}:${assignment.resource}`)) {
            pinDefines.push(`#define HYP_PERIPH_${macroBase} "${assignment.resource}"`);
            definedPeripherals.add(`${macroBase}:${assignment.resource}`);
        }
    }
    const resourceDefines = ['', '/* Hardware Setup resources — generated from project hardware.json */'];
    for (const resource of Object.values(hardware.resources || {})) {
        const macro = resource.id.toUpperCase().replace(/[^A-Z0-9_]/g, '_');
        const assigned = (hardware.assignments || []).filter(item => item.resource === resource.id);
        if (assigned.length > 0 || resource.type === 'accelerator') resourceDefines.push(`#define HYP_RESOURCE_${macro} 1`);
        for (const [key, value] of Object.entries(resource.configuration || {})) {
            const keyMacro = key.replace(/([a-z])([A-Z])/g, '$1_$2').toUpperCase().replace(/[^A-Z0-9_]/g, '_');
            resourceDefines.push(`#define HYP_RESOURCE_${macro}_${keyMacro} ${typeof value === 'number' ? value : JSON.stringify(String(value))}`);
        }
    }
    const marker = '#endif /* HYP_BOARD_CONFIG_H */';
    headerText = headerText.replace(marker, pinDefines.concat(resourceDefines).join('\n') + '\n\n' + marker);
    fs.writeFileSync(headerPath, headerText, 'utf8');
}

function loadProjectInputs(projectId) {
    const paths = projectPaths(projectId);
    readProjectManifest(projectId);
    const hardware = readJson(paths.hardware, 'project hardware');
    const graph = readJson(paths.graph, 'project graph');
    if (!hardware) throw new Error(`Project '${projectId}' has no hardware configuration.`);
    if (!graph) throw new Error(`Project '${projectId}' has no graph configuration.`);
    return { paths, hardware, graph: assertGraphDocument(graph) };
}

app.get('/api/hardware', (req, res) => {
    try { res.json(readHardwareConfig(requestedProjectId(req))); }
    catch (err) { res.status(400).json({ error: err.message }); }
});

app.post('/api/hardware', (req, res) => {
    try {
        const { board, assignments, configurations, devices } = req.body || {};
        if (!board || !Array.isArray(assignments)) return res.status(400).json({ error: 'Missing board or assignments.' });
        const hardware = projectHardware(board, assignments, configurations, devices);
        const projectId = requestedProjectId(req);
        if (projectId) readProjectManifest(projectId);
        writeHardwareConfig(hardware, projectId);
        res.json({ success: true, hardware });
    } catch (err) { res.status(400).json({ error: err.message }); }
});

function validateGraphHardwareResources(graph, projectId = null, hardwareOverride = null) {
    const hardware = hardwareOverride || readHardwareConfig(projectId);
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

function sendProjectError(res, err) {
    if (err && err.code === 'ENOENT') return res.status(404).json({ error: err.message });
    return res.status(400).json({ error: err.message || 'Project operation failed.' });
}

function touchProject(id) {
    const paths = projectPaths(id);
    const manifest = readProjectManifest(id);
    manifest.updatedAt = new Date().toISOString();
    writeJson(paths.manifest, manifest);
}

function listProjectFiles(directory) {
    try {
        return fs.readdirSync(directory, { withFileTypes: true })
            .filter(entry => entry.isFile())
            .map(entry => entry.name)
            .sort();
    } catch (_) { return []; }
}

function projectSourcePath(id, relativePath) {
    if (typeof relativePath !== 'string' || !relativePath || path.isAbsolute(relativePath)) {
        throw new Error('A relative project source path is required.');
    }
    const paths = projectPaths(id);
    const absolute = path.resolve(paths.projectDir, relativePath);
    if (!absolute.startsWith(paths.projectDir + path.sep)) throw new Error('Invalid project source path.');
    if (fs.existsSync(absolute)) {
        const resolved = fs.realpathSync(absolute);
        if (!resolved.startsWith(fs.realpathSync(paths.projectDir) + path.sep)) throw new Error('Invalid project source path.');
    }
    return absolute;
}

/* Persistent-project CRUD and component APIs. */
app.get('/api/projects', (_req, res) => {
    try {
        if (!fs.existsSync(PROJECTS_ROOT)) return res.json({ projects: [] });
        const projects = fs.readdirSync(PROJECTS_ROOT, { withFileTypes: true })
            .filter(entry => entry.isDirectory() && PROJECT_ID.test(entry.name))
            .map(entry => projectResponse(entry.name, false))
            .sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));
        res.json({ projects });
    } catch (err) { sendProjectError(res, err); }
});

app.post('/api/projects', (req, res) => {
    try { res.status(201).json({ project: createProject(req.body || {}) }); }
    catch (err) { sendProjectError(res, err); }
});

app.get('/api/projects/:id/status', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const project = projectResponse(id, false);
        const paths = projectPaths(id);
        res.json({
            project,
            generated: listProjectFiles(paths.generated),
            build: {
                log: fs.existsSync(paths.buildLog) ? 'build/build.log' : null,
                environments: fs.existsSync(paths.buildDir) ? fs.readdirSync(paths.buildDir, { withFileTypes: true })
                    .filter(entry => entry.isDirectory()).map(entry => entry.name).sort() : []
            }
        });
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/projects/:id/source', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        readProjectManifest(id);
        const source = projectSourcePath(id, req.query.path);
        if (!fs.existsSync(source) || !fs.statSync(source).isFile()) return res.status(404).json({ error: 'Project source file not found.' });
        res.json({ path: req.query.path, content: fs.readFileSync(source, 'utf8') });
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/projects/:id/build/log', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const paths = projectPaths(id);
        readProjectManifest(id);
        if (!fs.existsSync(paths.buildLog)) return res.status(404).json({ error: 'Project build log not found.' });
        res.type('text/plain').send(fs.readFileSync(paths.buildLog, 'utf8'));
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/projects/:id/build/firmware', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const paths = projectPaths(id);
        const inputs = loadProjectInputs(id);
        const environment = platformioEnvironment(inputs.hardware.board);
        const firmware = path.join(paths.buildDir, environment, 'firmware.bin');
        if (!fs.existsSync(firmware)) return res.status(404).json({ error: 'Project firmware artifact not found.' });
        res.type('application/octet-stream').send(fs.readFileSync(firmware));
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/projects/:id/hardware', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        readProjectManifest(id);
        const hardware = readJson(projectPaths(id).hardware, 'project hardware');
        if (!hardware) return res.status(404).json({ error: 'Project hardware has not been configured.' });
        res.json(hardware);
    } catch (err) { sendProjectError(res, err); }
});

app.put('/api/projects/:id/hardware', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        readProjectManifest(id);
        const hardware = canonicalHardware(req.body);
        writeHardwareConfig(hardware, id);
        touchProject(id);
        res.json({ success: true, hardware });
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/projects/:id/graph', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        readProjectManifest(id);
        const graph = readJson(projectPaths(id).graph, 'project graph');
        if (!graph) return res.status(404).json({ error: 'Project graph has not been configured.' });
        res.json(graph);
    } catch (err) { sendProjectError(res, err); }
});

app.put('/api/projects/:id/graph', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const project = writeProjectGraph(id, req.body);
        res.json({ success: true, graph: project.graph, project });
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/projects/:id', (req, res) => {
    try { res.json(projectResponse(assertProjectId(req.params.id))); }
    catch (err) { sendProjectError(res, err); }
});

app.put('/api/projects/:id', (req, res) => {
    try { res.json({ project: updateProject(assertProjectId(req.params.id), req.body) }); }
    catch (err) { sendProjectError(res, err); }
});

app.delete('/api/projects/:id', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const paths = projectPaths(id);
        readProjectManifest(id);
        fs.rmSync(paths.projectDir, { recursive: true, force: false });
        res.json({ success: true, id });
    } catch (err) { sendProjectError(res, err); }
});

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
        const projectId = requestedProjectId(req);
        const projectInputs = projectId ? loadProjectInputs(projectId) : null;
        const { board, assignments, configurations, devices } = projectInputs
            ? { board: projectInputs.hardware.board, assignments: projectInputs.hardware.assignments,
                configurations: Object.fromEntries(Object.entries(projectInputs.hardware.resources || {})
                    .map(([id, resource]) => [id, resource.configuration || {}])), devices: projectInputs.hardware.devices }
            : req.body;
        if (!board || !Array.isArray(assignments)) {
            return res.status(400).json({ error: 'Missing board or assignments.' });
        }

        const hardware = projectHardware(board, assignments, configurations, devices);
        if (projectId) readProjectManifest(projectId);
        writeHardwareConfig(hardware, projectId);
        if (projectId) touchProject(projectId);

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
    let projectId = null;
    let graph = req.body && req.body.graph ? req.body.graph : req.body;
    try {
        projectId = requestedProjectId(req);
        if (projectId) graph = loadProjectInputs(projectId).graph;
    } catch (err) { return sendProjectError(res, err); }
    if (!graph || typeof graph !== 'object' || !Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
        return res.status(400).json({ error: 'Body must be a graph with nodes and edges arrays.' });
    }

    let tempDir;
    try {
        validateGraphHardwareResources(graph, projectId);
        tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-mbd-build-'));
        const graphPath = path.join(tempDir, 'graph.json');
        const outputPath = path.join(tempDir, 'graph.c');
        fs.writeFileSync(graphPath, JSON.stringify(graph, null, 2), 'utf8');
        execFileSync(process.execPath, [GRAPH_CODEGEN, graphPath, outputPath], { encoding: 'utf8', stdio: 'pipe' });
        const source = fs.readFileSync(outputPath, 'utf8');
        if (projectId) writeProjectGraph(projectId, graph);
        res.json({ success: true, graph, source });
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

function materializeEsp32(graph, projectId = null) {
    let projectInputs = null;
    if (projectId) {
        projectInputs = loadProjectInputs(projectId);
        graph = projectInputs.graph;
    }
    const hardware = projectInputs ? projectInputs.hardware : readHardwareConfig();
    const boardKey = hardware.board || 'esp32';
    const targetGenerated = projectInputs ? projectInputs.paths.generated : ESP32_GENERATED;
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-esp32-'));
    try {
        validateGraphHardwareResources(graph, projectId, hardware);
        const graphPath = path.join(tempDir, 'graph.json');
        const graphSource = path.join(tempDir, 'graph.c');
        fs.writeFileSync(graphPath, JSON.stringify(graph, null, 2), 'utf8');
        execFileSync(process.execPath, [GRAPH_CODEGEN, graphPath, graphSource], { encoding: 'utf8', stdio: 'pipe' });
        const source = fs.readFileSync(graphSource, 'utf8');
        const step = graphStepSymbol(graph);

        fs.rmSync(targetGenerated, { recursive: true, force: true });
        fs.mkdirSync(targetGenerated, { recursive: true });
        fs.writeFileSync(path.join(targetGenerated, 'graph.c'), source, 'utf8');
        for (const file of ['hyp_esp32.c', 'hyp_router.c', 'hyp_cordic_ref.c', 'hyp_cordic_ref.h', 'hyp_esp32_hw.cpp', 'hyp_pid.c', 'hyp_encoder.c']) {
            fs.copyFileSync(path.join(REPO_ROOT, 'sdk/src', file), path.join(targetGenerated, file));
        }
        fs.copyFileSync(path.join(REPO_ROOT, 'sdk/include/hyprccel.h'), path.join(targetGenerated, 'hyprccel.h'));
        fs.copyFileSync(path.join(REPO_ROOT, 'sdk/include/hyp_esp32_hw.h'), path.join(targetGenerated, 'hyp_esp32_hw.h'));
        if (projectId) {
            const headerDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-board-header-'));
            try {
                execFileSync(process.execPath, [CODEGEN_JS, boardKey, BOARDS_YAML, headerDir], { encoding: 'utf8', stdio: 'pipe' });
                fs.copyFileSync(path.join(headerDir, 'hyp_board_config.h'), path.join(targetGenerated, 'hyp_board_config.h'));
            } finally {
                fs.rmSync(headerDir, { recursive: true, force: true });
            }
            injectHardwareHeader(path.join(targetGenerated, 'hyp_board_config.h'), hardware);
        } else {
            fs.copyFileSync(path.join(CODEGEN_OUT, 'hyp_board_config.h'), path.join(targetGenerated, 'hyp_board_config.h'));
        }
        fs.writeFileSync(path.join(targetGenerated, 'main.cpp'), `/* Generated MBD-T9 runtime wrapper; graph.c is the application logic. */
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
        if (projectId) {
            fs.writeFileSync(projectInputs.paths.platformio, projectPlatformioIni(hardware.board), 'utf8');
        }
        return {
            source,
            generatedDir: targetGenerated,
            platformio: projectId ? projectInputs.paths.platformio : path.join(ESP32_PROJECT, 'platformio.ini'),
            environment: platformioEnvironment(boardKey),
            board: boardKey
        };
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

function captureProjectBuildArtifacts(projectId, log) {
    if (!projectId) return;
    writeProjectBuildLog(projectId, log);
    const projectInputs = loadProjectInputs(projectId);
    const environment = platformioEnvironment(projectInputs.hardware.board);
    const firmware = path.join(projectInputs.paths.projectDir, '.pio', 'build', environment, 'firmware.bin');
    if (fs.existsSync(firmware)) {
        const destination = path.join(projectInputs.paths.buildDir, environment, 'firmware.bin');
        fs.mkdirSync(path.dirname(destination), { recursive: true });
        fs.copyFileSync(firmware, destination);
    }
}

function compileProject(projectId) {
    const inputs = loadProjectInputs(projectId);
    const generated = materializeEsp32(inputs.graph, projectId);
    const result = commandResult(platformioCommand(), ['run', '--project-dir', inputs.paths.projectDir]);
    captureProjectBuildArtifacts(projectId, result.output);
    touchProject(projectId);
    return { inputs, generated, result };
}

app.post('/api/projects/:id/generate', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const inputs = loadProjectInputs(id);
        const generated = materializeEsp32(inputs.graph, id);
        res.json({ success: true, project: projectResponse(id), generated });
    } catch (err) { sendProjectError(res, err); }
});

app.post('/api/projects/:id/compile', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const build = compileProject(id);
        res.status(build.result.ok ? 200 : 422).json({
            success: build.result.ok, stage: 'Compiling', project: projectResponse(id, false),
            generated: build.generated, log: build.result.output
        });
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/esp32/ports', (_req, res) => {
    res.json({ configuredPort: ESP32_PORT || null, candidates: serialCandidates() });
});

app.post('/api/compile', (req, res) => {
    const requestedId = (() => {
        try { return requestedProjectId(req); } catch (err) { res.status(400).json({ error: err.message }); return undefined; }
    })();
    if (requestedId) {
        try {
            const build = compileProject(requestedId);
            return res.status(build.result.ok ? 200 : 422).json({
                success: build.result.ok, stage: 'Compiling', project: projectResponse(requestedId, false),
                generated: build.generated, log: build.result.output
            });
        } catch (err) { return sendProjectError(res, err); }
    }
    const graph = validGraphBody(req, res);
    if (!graph) return;
    try {
        const generated = materializeEsp32(graph);
        const compile = commandResult(platformioCommand(), ['run', '--project-dir', ESP32_PROJECT]);
        const status = compile.ok ? 200 : 422;
        fs.writeFileSync(path.join(HYPRACCEL_DIR, 'build.log'), compile.output, 'utf8');
        res.status(status).json({ success: compile.ok, stage: 'Compiling', source: generated.source, log: compile.output });
    } catch (err) {
        const detail = err.stderr ? String(err.stderr).trim() : err.message;
        res.status(422).json({ error: detail || 'ESP32 source preparation failed.' });
    }
});

app.post('/api/flash', (req, res) => {
    let requestedId;
    try { requestedId = requestedProjectId(req); } catch (err) { return res.status(400).json({ error: err.message }); }
    if (requestedId) {
        try {
            const build = compileProject(requestedId);
            if (!build.result.ok) return res.status(422).json({ success: false, stage: 'Compiling', log: build.result.output });
            const selection = selectedSerialPort(req.body.port);
            if (selection.error) return res.status(422).json({ success: false, stage: 'Flashing', log: `${build.result.output}\n${selection.error}` });
            const flash = commandResult(platformioCommand(), ['run', '--project-dir', build.inputs.paths.projectDir, '--target', 'upload', '--upload-port', selection.port]);
            const log = `${build.result.output}\n\n${flash.output}`;
            captureProjectBuildArtifacts(requestedId, log);
            return res.status(flash.ok ? 200 : 422).json({ success: flash.ok, stage: flash.ok ? 'Flashed' : 'Flashing', project: projectResponse(requestedId, false), log });
        } catch (err) { return sendProjectError(res, err); }
    }
    const graph = validGraphBody(req, res);
    if (!graph) return;
    try {
        const generated = materializeEsp32(graph);
        const compile = commandResult(platformioCommand(), ['run', '--project-dir', ESP32_PROJECT]);
        if (!compile.ok) {
            fs.writeFileSync(path.join(HYPRACCEL_DIR, 'build.log'), compile.output, 'utf8');
            return res.status(422).json({ success: false, stage: 'Compiling', source: generated.source, log: compile.output });
        }
        const selection = selectedSerialPort(req.body.port);
        if (selection.error) {
            const log = `${compile.output}\n${selection.error}`;
            fs.writeFileSync(path.join(HYPRACCEL_DIR, 'build.log'), log, 'utf8');
            return res.status(422).json({ success: false, stage: 'Flashing', source: generated.source, log: `${compile.output}\n${selection.error}` });
        }
        const flash = commandResult(platformioCommand(), ['run', '--project-dir', ESP32_PROJECT, '--target', 'upload', '--upload-port', selection.port]);
        const logContent = `${compile.output}\n\n${flash.output}`;
        fs.writeFileSync(path.join(HYPRACCEL_DIR, 'build.log'), logContent, 'utf8');
        res.status(flash.ok ? 200 : 422).json({ success: flash.ok, stage: flash.ok ? 'Flashed' : 'Flashing', source: generated.source, port: selection.port, log: logContent });
    } catch (err) {
        const detail = err.stderr ? String(err.stderr).trim() : err.message;
        res.status(422).json({ error: detail || 'ESP32 flash preparation failed.' });
    }
});

/* Read the board's generated runtime banner and first publish event.  The
 * monitor intentionally times out; seeing all markers is the success signal. */
app.post('/api/verify', (req, res) => {
    let projectId;
    let graph;
    try {
        projectId = requestedProjectId(req);
        graph = projectId ? loadProjectInputs(projectId).graph : validGraphBody(req, res);
    } catch (err) { return sendProjectError(res, err); }
    if (!graph) return;
    try {
        const selection = selectedSerialPort(req.body.port);
        if (selection.error) {
            fs.writeFileSync(path.join(HYPRACCEL_DIR, 'build.log'), selection.error, 'utf8');
            writeProjectBuildLog(projectId, selection.error);
            return res.status(422).json({ success: false, stage: 'Verifying', log: selection.error });
        }
        const markers = graphVerificationMarkers(graph);
        const monitor = commandResult('timeout', ['10s', platformioCommand(), 'device', 'monitor', '--port', selection.port, '--baud', '115200'], { timeout: 15000 });
        const missing = markers.filter(marker => !monitor.output.includes(marker));
        const verified = missing.length === 0;
        fs.writeFileSync(path.join(HYPRACCEL_DIR, 'build.log'), monitor.output, 'utf8');
        writeProjectBuildLog(projectId, monitor.output);
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

/* Legacy workspace aliases now require an explicit persistent project.  They
 * intentionally do not recreate the former singleton .hypraccel/project.json. */
app.post('/api/project/generate', (req, res) => {
    try {
        const projectId = requestedProjectId(req);
        if (!projectId) return res.status(400).json({ error: 'projectId is required; use /api/projects to create a project.' });
        const graph = req.body.graph || req.body;
        writeProjectGraph(projectId, graph);
        materializeEsp32(graph, projectId);
        res.json({ success: true, project: projectResponse(projectId) });
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/project', (req, res) => {
    try {
        const projectId = requestedProjectId(req);
        if (!projectId) return res.status(400).json({ error: 'projectId is required; use /api/projects.' });
        res.json(projectResponse(projectId));
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/project/file', (req, res) => {
    try {
        const projectId = requestedProjectId(req);
        if (!projectId) return res.status(400).json({ error: 'projectId is required; use /api/projects/:id/source.' });
        readProjectManifest(projectId);
        const source = projectSourcePath(projectId, req.query.path);
        if (!fs.existsSync(source) || !fs.statSync(source).isFile()) return res.status(404).json({ error: 'File not found.' });
        res.json({ content: fs.readFileSync(source, 'utf8') });
    } catch (err) { sendProjectError(res, err); }
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
