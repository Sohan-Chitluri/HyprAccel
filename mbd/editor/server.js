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
const yaml    = require('js-yaml');
const { readProjectConfig, readClockConfig, mergeProjectConfig } = require('../../boards/codegen/project_config');
const { checkPinConflicts } = require('../../boards/codegen/pin_conflicts');
const { injectProjectDefines } = require('../../boards/codegen/project_defines');

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
// Graph identifiers deliberately use the same conservative grammar as project
// identifiers.  They are filename stems, never browser-supplied paths.
const GRAPH_ID = /^[A-Za-z][A-Za-z0-9_-]{0,63}$/;
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

app.get('/graph_editor', (req, res) => {
    res.sendFile(path.join(__dirname, 'src/graph_editor.html'));
});

app.get('/workspace', (req, res) => {
    res.sendFile(path.join(__dirname, 'src/workspace.html'));
});


/* --------------------------------------------------------------------------
 * GET /api/boards
 * Returns parsed boards.yaml as JSON for the UI to build its pin picker.
 * ----------------------------------------------------------------------- */
app.get('/api/boards', (req, res) => {
    try {
        const parsed  = yaml.load(fs.readFileSync(BOARDS_YAML, 'utf8'));
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

        const parsed  = yaml.load(fs.readFileSync(BOARDS_YAML, 'utf8'));
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
                /* "devices" is a nested map of named I2C slaves (SDK-T6), not a
                 * pin signal — exclude it from the bus pin assignments. */
                assignments: Object.entries(signals)
                    .filter(([role]) => role !== 'devices')
                    .map(([role, pin]) => ({ role, pin })), configuration: {}
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
    const parsed = yaml.load(fs.readFileSync(BOARDS_YAML, 'utf8'));
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

function assertGraphId(id) {
    if (typeof id !== 'string' || !GRAPH_ID.test(id)) {
        throw new Error('Graph id must start with a letter and contain only letters, numbers, hyphens, or underscores.');
    }
    return id;
}

function graphDisplayName(name, fallback) {
    if (name == null || name === '') return fallback;
    if (typeof name !== 'string' || !name.trim() || name.trim().length > 120) {
        throw new Error('Graph name must be a non-empty string no longer than 120 characters.');
    }
    return name.trim();
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
        // legacyGraph is kept solely for lazy, non-destructive migration.
        legacyGraph: path.join(projectDir, 'graph', 'graph.json'),
        graphsDir: path.join(projectDir, 'graphs'),
        generated: path.join(projectDir, 'generated'),
        platformio: path.join(projectDir, 'platformio.ini'),
        buildDir: path.join(projectDir, 'build'),
        buildLog: path.join(projectDir, 'build', 'build.log')
    };
}

function projectGraphPath(id, graphId) {
    const paths = projectPaths(id);
    graphId = assertGraphId(graphId);
    const projectReal = fs.realpathSync(paths.projectDir);
    if (fs.existsSync(paths.graphsDir)) {
        const graphsReal = fs.realpathSync(paths.graphsDir);
        if (!graphsReal.startsWith(projectReal + path.sep)) throw new Error('Invalid graph directory.');
    }
    const graphPath = path.resolve(paths.graphsDir, `${graphId}.json`);
    if (!graphPath.startsWith(paths.graphsDir + path.sep)) throw new Error('Invalid graph path.');
    if (fs.existsSync(graphPath) && fs.lstatSync(graphPath).isSymbolicLink()) throw new Error('Graph files may not be symbolic links.');
    return graphPath;
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

function requestedGraphId(req) {
    const candidate = req.query && req.query.graphId || req.body && req.body.graphId;
    return candidate == null || candidate === '' ? null : assertGraphId(candidate);
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
        const parsed = yaml.load(fs.readFileSync(BOARDS_YAML, 'utf8'));
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
    const parsed = yaml.load(fs.readFileSync(BOARDS_YAML, 'utf8'));
    // Own-property lookup: keys such as 'toString' must not resolve to Object.prototype.
    const board = Object.prototype.hasOwnProperty.call(parsed.boards, boardKey) ? parsed.boards[boardKey] : null;
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
 * Hardware lives only in hardware/hardware.json.  Graph documents are
 * canonical files in graphs/<graphId>.json; the manifest stores only the
 * active/artifact graph references needed to describe project state.
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
    return migrateLegacyProjectGraph(id, manifest);
}

/* Legacy projects had a sole graph at graph/graph.json.  Migration is lazy:
 * the first project operation copies that valid document into the graph-file
 * store, updates only manifest references, and deliberately leaves the legacy
 * file untouched as a recoverable compatibility copy. */
function migrateLegacyProjectGraph(id, manifest) {
    const paths = projectPaths(id);
    if (manifest.graphs && manifest.graphs.path === 'graphs') return manifest;
    const legacy = readJson(paths.legacyGraph, 'legacy project graph');
    if (legacy) {
        const graph = assertGraphDocument(legacy);
        const graphId = assertGraphId(graph.id);
        const destination = projectGraphPath(id, graphId);
        if (!fs.existsSync(destination)) writeJson(destination, graph);
        manifest.activeGraphId = manifest.activeGraphId || graphId;
    }
    delete manifest.graph;
    manifest.graphs = { path: 'graphs' };
    manifest.updatedAt = manifest.updatedAt || new Date().toISOString();
    writeJson(paths.manifest, manifest);
    return manifest;
}

function listProjectGraphs(id) {
    const paths = projectPaths(id);
    readProjectManifest(id);
    try {
        return fs.readdirSync(paths.graphsDir, { withFileTypes: true })
            .filter(entry => entry.isFile() && entry.name.endsWith('.json'))
            .map(entry => {
                const graphId = entry.name.slice(0, -'.json'.length);
                if (!GRAPH_ID.test(graphId)) return null;
                const graph = readJson(path.join(paths.graphsDir, entry.name), `graph '${graphId}'`);
                if (!graph) return null;
                assertGraphDocument(graph);
                if (graph.id !== graphId) throw new Error(`Graph file '${entry.name}' has an id that does not match its filename.`);
                return { id: graphId, name: graphDisplayName(graph.name, graphId), filename: entry.name };
            })
            .filter(Boolean)
            .sort((a, b) => a.filename.localeCompare(b.filename));
    } catch (err) {
        if (err.code === 'ENOENT') return [];
        throw err;
    }
}

function readProjectGraph(id, graphId) {
    readProjectManifest(id);
    const graph = readJson(projectGraphPath(id, graphId), `project graph '${graphId}'`);
    if (!graph) {
        const err = new Error(`Graph '${graphId}' was not found in project '${id}'.`);
        err.code = 'ENOENT';
        throw err;
    }
    assertGraphDocument(graph);
    if (graph.id !== graphId) throw new Error(`Graph '${graphId}' has an inconsistent document id.`);
    return graph;
}

function resolveProjectGraphId(id, graphId = null) {
    const manifest = readProjectManifest(id);
    if (graphId != null && graphId !== '') return assertGraphId(graphId);
    if (manifest.activeGraphId) return assertGraphId(manifest.activeGraphId);
    const graphs = listProjectGraphs(id);
    if (graphs.length) return graphs[0].id;
    const err = new Error(`Project '${id}' has no graph files.`);
    err.code = 'ENOENT';
    throw err;
}

function updateProjectGraphReferences(id, changes) {
    const paths = projectPaths(id);
    const manifest = readProjectManifest(id);
    Object.assign(manifest, changes, { updatedAt: new Date().toISOString() });
    writeJson(paths.manifest, manifest);
    return manifest;
}

function invalidateProjectArtifactsForGraph(id, graphId) {
    const paths = projectPaths(id);
    const manifest = readProjectManifest(id);
    if (manifest.generatedGraphId !== graphId && manifest.buildGraphId !== graphId) return;
    fs.rmSync(paths.generated, { recursive: true, force: true });
    fs.rmSync(paths.buildDir, { recursive: true, force: true });
    if (manifest.generatedGraphId === graphId) manifest.generatedGraphId = null;
    if (manifest.buildGraphId === graphId) manifest.buildGraphId = null;
    manifest.updatedAt = new Date().toISOString();
    writeJson(paths.manifest, manifest);
}

function projectResponse(id, includeComponents = true) {
    const manifest = readProjectManifest(id);
    const paths = projectPaths(id);
    const hardware = readJson(paths.hardware, 'project hardware');
    const graphs = listProjectGraphs(id);
    const activeGraphId = manifest.activeGraphId && graphs.some(graph => graph.id === manifest.activeGraphId)
        ? manifest.activeGraphId : (graphs[0] && graphs[0].id || null);
    const graph = activeGraphId ? readProjectGraph(id, activeGraphId) : null;
    const response = {
        ...manifest,
        board: hardware && hardware.board || null,
        activeGraphId,
        graphId: activeGraphId, // temporary response compatibility for existing project clients
        graphs,
        components: { hardware: Boolean(hardware), graph: Boolean(graph), graphs: graphs.length > 0 }
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
    if (input.board != null && typeof input.board !== 'string') {
        throw new Error('Project board must be a string.');
    }
    if (input.board != null && input.hardware != null && input.hardware.board !== input.board) {
        throw new Error(`Board '${input.board}' does not match hardware board '${input.hardware.board}'.`);
    }
    // Resolve/validate hardware up front so an invalid board or hardware payload
    // never leaves a partial project directory behind.
    let hardware = null;
    if (input.hardware != null) hardware = canonicalHardware(input.hardware);
    else if (input.board != null) hardware = projectHardware(input.board, []);
    const paths = projectPaths(id);
    if (fs.existsSync(paths.projectDir)) {
        const err = new Error(`Project '${id}' already exists.`);
        err.code = 'EEXIST';
        throw err;
    }
    const now = new Date().toISOString();
    const manifest = {
        format: 'hypraccel.project', version: 1, id, name: input.name.trim(),
        hardware: { path: 'hardware/hardware.json' },
        graphs: { path: 'graphs' },
        createdAt: now, updatedAt: now
    };
    fs.mkdirSync(paths.projectDir, { recursive: true });
    try {
        writeJson(paths.manifest, manifest);
        fs.mkdirSync(paths.graphsDir, { recursive: true });
        if (hardware != null) writeHardwareConfig(hardware, id);
        if (input.graph != null) {
            const graph = assertGraphDocument(input.graph);
            writeJson(projectGraphPath(id, graph.id), graph);
            manifest.activeGraphId = graph.id;
            writeJson(paths.manifest, manifest);
        }
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
    if (input.graph != null) {
        const graph = assertGraphDocument(input.graph);
        writeProjectGraph(id, graph.id, graph, { create: !fs.existsSync(projectGraphPath(id, graph.id)) });
    }
    manifest.updatedAt = new Date().toISOString();
    writeJson(paths.manifest, manifest);
    return projectResponse(id);
}

function writeProjectGraph(id, graphId, graph, options = {}) {
    readProjectManifest(id);
    graphId = assertGraphId(graphId);
    graph = assertGraphDocument(graph);
    if (graph.id !== graphId) throw new Error('Graph document id must match the graph file id.');
    graph.name = graphDisplayName(graph.name, graphId);
    const graphPath = projectGraphPath(id, graphId);
    if (options.create && fs.existsSync(graphPath)) {
        const err = new Error(`Graph '${graphId}' already exists.`);
        err.code = 'EEXIST';
        throw err;
    }
    writeJson(graphPath, graph);
    invalidateProjectArtifactsForGraph(id, graphId);
    updateProjectGraphReferences(id, { activeGraphId: graphId });
    return { graph, project: projectResponse(id) };
}

function writeProjectBuildLog(id, content) {
    if (!id) return;
    readProjectManifest(id);
    const paths = projectPaths(id);
    fs.mkdirSync(paths.buildDir, { recursive: true });
    fs.writeFileSync(paths.buildLog, content, 'utf8');
}

function boardDescriptor(boardKey) {
    const parsed = yaml.load(fs.readFileSync(BOARDS_YAML, 'utf8'));
    // Own-property lookup: keys such as 'toString' must not resolve to Object.prototype.
    const board = Object.prototype.hasOwnProperty.call(parsed.boards, boardKey) ? parsed.boards[boardKey] : null;
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
        `    -I generated\n`;
}

function loadProjectInputs(projectId, graphId = null) {
    const paths = projectPaths(projectId);
    readProjectManifest(projectId);
    if (!fs.existsSync(paths.hardware)) throw new Error(`Project '${projectId}' has no hardware configuration.`);
    const hardware = readProjectConfig(paths.projectDir, BOARDS_YAML);
    const selectedGraphId = resolveProjectGraphId(projectId, graphId);
    const graph = readProjectGraph(projectId, selectedGraphId);
    return { paths, hardware, graphId: selectedGraphId, graph: assertGraphDocument(graph) };
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
    if (err && (err.code === 'EEXIST' || err.code === 'EINVARIANT')) return res.status(409).json({ error: err.message });
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
            graphArtifacts: {
                activeGraphId: project.activeGraphId,
                generatedGraphId: project.generatedGraphId || null,
                buildGraphId: project.buildGraphId || null
            },
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

/* Project-local graph file API.  graphId is always an identifier, never a
 * relative filename or path supplied by the browser. */
app.get('/api/projects/:id/graphs', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const manifest = readProjectManifest(id);
        res.json({ graphs: listProjectGraphs(id), activeGraphId: manifest.activeGraphId || null });
    } catch (err) { sendProjectError(res, err); }
});

app.get('/api/projects/:id/graphs/:graphId', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const graphId = assertGraphId(req.params.graphId);
        res.json(readProjectGraph(id, graphId));
    } catch (err) { sendProjectError(res, err); }
});

app.post('/api/projects/:id/graphs', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const body = req.body || {};
        const graph = body.graph || body;
        const graphId = assertGraphId(body.graphId || graph.id);
        graph.id = graphId;
        graph.name = graphDisplayName(body.name || graph.name, graphId);
        const saved = writeProjectGraph(id, graphId, graph, { create: true });
        res.status(201).json({ success: true, graph: saved.graph, project: saved.project });
    } catch (err) { sendProjectError(res, err); }
});

app.put('/api/projects/:id/graphs/:graphId', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const graphId = assertGraphId(req.params.graphId);
        const body = req.body || {};
        const graph = body.graph || body;
        graph.id = graphId;
        if (body.name != null) graph.name = graphDisplayName(body.name, graphId);
        const saved = writeProjectGraph(id, graphId, graph);
        res.json({ success: true, graph: saved.graph, project: saved.project });
    } catch (err) { sendProjectError(res, err); }
});

app.post('/api/projects/:id/graphs/:graphId/activate', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const graphId = assertGraphId(req.params.graphId);
        readProjectGraph(id, graphId);
        updateProjectGraphReferences(id, { activeGraphId: graphId });
        res.json({ success: true, activeGraphId: graphId });
    } catch (err) { sendProjectError(res, err); }
});

app.put('/api/projects/:id/graphs/:graphId/rename', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const oldGraphId = assertGraphId(req.params.graphId);
        const newGraphId = assertGraphId(req.body && req.body.graphId);
        if (newGraphId === oldGraphId) return res.json({ success: true, graph: readProjectGraph(id, oldGraphId), project: projectResponse(id) });
        const oldPath = projectGraphPath(id, oldGraphId);
        const newPath = projectGraphPath(id, newGraphId);
        const graph = readProjectGraph(id, oldGraphId);
        if (fs.existsSync(newPath)) {
            const err = new Error(`Graph '${newGraphId}' already exists.`);
            err.code = 'EEXIST';
            throw err;
        }
        graph.id = newGraphId;
        graph.name = graphDisplayName(req.body && req.body.name, graph.name || newGraphId);
        writeJson(newPath, graph);
        fs.unlinkSync(oldPath);
        const manifest = readProjectManifest(id);
        const changes = {};
        for (const key of ['activeGraphId', 'generatedGraphId', 'buildGraphId']) {
            if (manifest[key] === oldGraphId) changes[key] = key === 'activeGraphId' ? newGraphId : null;
        }
        if (manifest.generatedGraphId === oldGraphId || manifest.buildGraphId === oldGraphId) {
            fs.rmSync(projectPaths(id).generated, { recursive: true, force: true });
            fs.rmSync(projectPaths(id).buildDir, { recursive: true, force: true });
        }
        updateProjectGraphReferences(id, changes);
        res.json({ success: true, graph, project: projectResponse(id) });
    } catch (err) { sendProjectError(res, err); }
});

app.delete('/api/projects/:id/graphs/:graphId', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const graphId = assertGraphId(req.params.graphId);
        const graphs = listProjectGraphs(id);
        if (!graphs.some(graph => graph.id === graphId)) {
            const err = new Error(`Graph '${graphId}' was not found in project '${id}'.`);
            err.code = 'ENOENT';
            throw err;
        }
        if (graphs.length <= 1) {
            const err = new Error('A project must retain at least one graph file. Create another graph before deleting this one.');
            err.code = 'EINVARIANT';
            throw err;
        }
        fs.unlinkSync(projectGraphPath(id, graphId));
        const manifest = readProjectManifest(id);
        const nextGraphId = graphs.find(graph => graph.id !== graphId).id;
        const changes = {};
        if (manifest.activeGraphId === graphId) changes.activeGraphId = nextGraphId;
        if (manifest.generatedGraphId === graphId || manifest.buildGraphId === graphId) {
            fs.rmSync(projectPaths(id).generated, { recursive: true, force: true });
            fs.rmSync(projectPaths(id).buildDir, { recursive: true, force: true });
            changes.generatedGraphId = null;
            changes.buildGraphId = null;
        }
        updateProjectGraphReferences(id, changes);
        res.json({ success: true, activeGraphId: projectResponse(id, false).activeGraphId });
    } catch (err) { sendProjectError(res, err); }
});

/* Compatibility aliases select the manifest's explicit active graph; all
 * first-party UI flows use the file API above and pass graphId explicitly. */
app.get('/api/projects/:id/graph', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        res.json(readProjectGraph(id, resolveProjectGraphId(id)));
    } catch (err) { sendProjectError(res, err); }
});

app.put('/api/projects/:id/graph', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const graph = req.body || {};
        const graphId = resolveProjectGraphId(id, graph.id);
        graph.id = graphId;
        const saved = writeProjectGraph(id, graphId, graph);
        res.json({ success: true, graph: saved.graph, project: saved.project });
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
        // Hardware generation is project-scoped but graph-independent.
        const projectInputs = projectId ? { hardware: readHardwareConfig(projectId) } : null;
        const { board, assignments, configurations, devices } = projectInputs
            ? { board: projectInputs.hardware.board, assignments: projectInputs.hardware.assignments,
                configurations: Object.fromEntries(Object.entries(projectInputs.hardware.resources || {})
                    .map(([id, resource]) => [id, resource.configuration || {}])), devices: projectInputs.hardware.devices }
            : req.body;
        if (!board || !Array.isArray(assignments)) {
            return res.status(400).json({ error: 'Missing board or assignments.' });
        }

        const hardware = projectHardware(board, assignments, configurations, devices);
        const boardData = boardDescriptor(board);
        const clock = projectId ? readClockConfig(projectPaths(projectId).projectDir) : null;
        const config = mergeProjectConfig(hardware, clock, boardData);
        const conflicts = checkPinConflicts(config, boardData, []);
        if (conflicts.errors.length > 0) {
            return res.status(400).json({ error: conflicts.errors.map(issue => issue.message).join('; '), issues: conflicts.errors });
        }
        const warnings = conflicts.warnings.map(issue => issue.message);

        if (projectId) readProjectManifest(projectId);
        writeHardwareConfig(hardware, projectId);
        if (projectId) touchProject(projectId);

        /* Run the existing codegen script */
        const cmd = `node "${CODEGEN_JS}" "${board}" "${BOARDS_YAML}" "${CODEGEN_OUT}"`;
        execSync(cmd, { stdio: 'pipe' });

        /* Read the generated base header */
        const headerPath  = path.join(CODEGEN_OUT, 'hyp_board_config.h');
        let   headerText  = fs.readFileSync(headerPath, 'utf8');

        headerText = injectProjectDefines(headerText, config, boardData, { variant: 'api', warnings });
        fs.writeFileSync(headerPath, headerText, 'utf8');

        res.json({ success: true, header: headerText, assignments: hardware.assignments, hardware, warnings });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

/* --------------------------------------------------------------------------
 * POST /api/hardware/check
 * Body: { board, assignments, configurations?, devices?, projectId? } — same
 * shape /api/generate takes. Runs the SAME mergeProjectConfig →
 * checkPinConflicts chain /api/generate uses, so the UIs can show live
 * pin-conflict feedback as assignments change, without writing anything.
 * Deliberately does NOT call readHardwareConfig() (which has the side effect
 * of rewriting hardware.json on migration) — projectHardware() builds the
 * canonical hardware object straight from the request body instead.
 * ----------------------------------------------------------------------- */
app.post('/api/hardware/check', (req, res) => {
    try {
        const projectId = requestedProjectId(req);
        const { board, assignments, configurations, devices } = req.body || {};
        if (!board || !Array.isArray(assignments)) {
            return res.status(400).json({ error: 'Missing board or assignments.' });
        }
        const hardware = projectHardware(board, assignments, configurations || {}, devices || []);
        const boardData = boardDescriptor(board);
        const clock = projectId ? readClockConfig(projectPaths(projectId).projectDir) : null;
        const config = mergeProjectConfig(hardware, clock, boardData);
        let graphs = [];
        if (projectId) {
            readProjectManifest(projectId);
            graphs = listProjectGraphs(projectId).map(entry => readProjectGraph(projectId, entry.id));
        }
        const conflicts = checkPinConflicts(config, boardData, graphs);
        res.json({ errors: conflicts.errors, warnings: conflicts.warnings });
    } catch (err) {
        res.status(400).json({ error: err.message || 'Pin conflict check failed.' });
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
    let graphId = null;
    let graph = req.body && req.body.graph ? req.body.graph : req.body;
    try {
        projectId = requestedProjectId(req);
        graphId = requestedGraphId(req);
        if (projectId) graph = loadProjectInputs(projectId, graphId).graph;
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

function materializeEsp32(graph, projectId = null, graphId = null) {
    let projectInputs = null;
    if (projectId) {
        projectInputs = loadProjectInputs(projectId, graphId);
        graph = projectInputs.graph;
    }
    const hardware = projectInputs ? projectInputs.hardware : readHardwareConfig();
    const boardKey = hardware.board || 'esp32';
    const targetGenerated = projectInputs ? projectInputs.paths.generated : ESP32_GENERATED;
    const boardData = boardDescriptor(boardKey);
    const conflicts = checkPinConflicts(hardware, boardData, [graph]);
    if (conflicts.errors.length > 0) {
        const err = new Error(conflicts.errors.map(issue => issue.message).join('; '));
        err.code = 'EINVARIANT';
        throw err;
    }
    const pinWarnings = conflicts.warnings.map(issue => issue.message);
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
        const headerDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-board-header-'));
        try {
            execFileSync(process.execPath, [CODEGEN_JS, boardKey, BOARDS_YAML, headerDir], { encoding: 'utf8', stdio: 'pipe' });
            fs.copyFileSync(path.join(headerDir, 'hyp_board_config.h'), path.join(targetGenerated, 'hyp_board_config.h'));
        } finally {
            fs.rmSync(headerDir, { recursive: true, force: true });
        }
        const boardHeaderPath = path.join(targetGenerated, 'hyp_board_config.h');
        const boardHeaderText = fs.readFileSync(boardHeaderPath, 'utf8');
        fs.writeFileSync(boardHeaderPath, injectProjectDefines(boardHeaderText, hardware, boardData, { variant: 'materialize', warnings: pinWarnings }), 'utf8');
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

    Serial.flush();
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
            const manifest = readProjectManifest(projectId);
            if (manifest.generatedGraphId && manifest.generatedGraphId !== projectInputs.graphId) {
                fs.rmSync(projectInputs.paths.buildDir, { recursive: true, force: true });
                manifest.buildGraphId = null;
            }
            manifest.generatedGraphId = projectInputs.graphId;
            manifest.updatedAt = new Date().toISOString();
            writeJson(projectInputs.paths.manifest, manifest);
        }
        return {
            source,
            graphId: projectInputs ? projectInputs.graphId : graph.id,
            generatedDir: targetGenerated,
            platformio: projectId ? projectInputs.paths.platformio : path.join(ESP32_PROJECT, 'platformio.ini'),
            environment: platformioEnvironment(boardKey),
            board: boardKey,
            warnings: pinWarnings
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

function captureProjectBuildArtifacts(projectId, graphId, log) {
    if (!projectId) return;
    writeProjectBuildLog(projectId, log);
    const projectInputs = loadProjectInputs(projectId, graphId);
    const environment = platformioEnvironment(projectInputs.hardware.board);
    const firmware = path.join(projectInputs.paths.projectDir, '.pio', 'build', environment, 'firmware.bin');
    if (fs.existsSync(firmware)) {
        const destination = path.join(projectInputs.paths.buildDir, environment, 'firmware.bin');
        fs.mkdirSync(path.dirname(destination), { recursive: true });
        fs.copyFileSync(firmware, destination);
    }
    updateProjectGraphReferences(projectId, { buildGraphId: projectInputs.graphId });
}

function compileProject(projectId, graphId = null) {
    const inputs = loadProjectInputs(projectId, graphId);
    const generated = materializeEsp32(inputs.graph, projectId, inputs.graphId);
    const result = commandResult(platformioCommand(), ['run', '--project-dir', inputs.paths.projectDir]);
    captureProjectBuildArtifacts(projectId, inputs.graphId, result.output);
    return { inputs, generated, result };
}

app.post('/api/projects/:id/generate', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const inputs = loadProjectInputs(id, requestedGraphId(req));
        const generated = materializeEsp32(inputs.graph, id, inputs.graphId);
        res.json({ success: true, project: projectResponse(id), generated });
    } catch (err) { sendProjectError(res, err); }
});

app.post('/api/projects/:id/compile', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const build = compileProject(id, requestedGraphId(req));
        res.status(build.result.ok ? 200 : 422).json({
            success: build.result.ok, stage: 'Compiling', project: projectResponse(id, false),
            generated: build.generated, log: build.result.output
        });
    } catch (err) { sendProjectError(res, err); }
});

/* Explicit graph-scoped generation/build routes used by the editor and
 * workspace.  The project-only variants above remain compatibility aliases. */
app.post('/api/projects/:id/graphs/:graphId/generate', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const graphId = assertGraphId(req.params.graphId);
        const inputs = loadProjectInputs(id, graphId);
        const generated = materializeEsp32(inputs.graph, id, graphId);
        res.json({ success: true, project: projectResponse(id), generated });
    } catch (err) { sendProjectError(res, err); }
});

app.post('/api/projects/:id/graphs/:graphId/compile', (req, res) => {
    try {
        const id = assertProjectId(req.params.id);
        const build = compileProject(id, assertGraphId(req.params.graphId));
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
            const build = compileProject(requestedId, requestedGraphId(req));
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
    let requestedGraph;
    try { requestedId = requestedProjectId(req); requestedGraph = requestedGraphId(req); } catch (err) { return res.status(400).json({ error: err.message }); }
    if (requestedId) {
        try {
            const build = compileProject(requestedId, requestedGraph);
            if (!build.result.ok) return res.status(422).json({ success: false, stage: 'Compiling', log: build.result.output });
            const selection = selectedSerialPort(req.body.port);
            if (selection.error) return res.status(422).json({ success: false, stage: 'Flashing', log: `${build.result.output}\n${selection.error}` });
            const flash = commandResult(platformioCommand(), ['run', '--project-dir', build.inputs.paths.projectDir, '--target', 'upload', '--upload-port', selection.port]);
            const log = `${build.result.output}\n\n${flash.output}`;
            captureProjectBuildArtifacts(requestedId, build.inputs.graphId, log);
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
        graph = projectId ? loadProjectInputs(projectId, requestedGraphId(req)).graph : validGraphBody(req, res);
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
        const graphId = requestedGraphId(req) || graph.id;
        writeProjectGraph(projectId, graphId, graph, { create: !fs.existsSync(projectGraphPath(projectId, graphId)) });
        materializeEsp32(graph, projectId, graphId);
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
    defaultResourceConfig,
    projectHardware,
    validateGraphHardwareResources,
    readHardwareConfig,
    writeHardwareConfig
};
