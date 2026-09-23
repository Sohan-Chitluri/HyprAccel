'use strict';

/*
 * desktop_codegen.test.js — integration test for desktop-saved projects
 * (hardware/hardware.json with pin assignments + hardware/clock.json) flowing
 * through the SAME codegen path the web editor uses (gen_board_config.js
 * --project, POST /api/projects/:id/generate, POST /api/generate?projectId=).
 *
 * The project directory below is written exactly the way desktop's
 * ProjectStore (desktop/src/project/project_store.cpp) writes it: a
 * project.json manifest, hardware/hardware.json with assignments whose node
 * is "", a full resources map, devices [], and hardware/clock.json with
 * version/board/selections/resolved/errors.
 */
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const { execFileSync } = require('child_process');
const yaml = require('js-yaml');

const REPO = path.resolve(__dirname, '../../..');
const BOARDS_YAML = path.join(REPO, 'boards/boards.yaml');
const GEN_BOARD_CONFIG = path.join(REPO, 'boards/codegen/gen_board_config.js');

const testRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-desktop-codegen-'));
process.env.HYPRACCEL_PROJECTS_ROOT = path.join(testRoot, 'projects');
process.env.HYPRACCEL_HARDWARE_CONFIG = path.join(testRoot, 'legacy-hardware.json');
fs.mkdirSync(process.env.HYPRACCEL_PROJECTS_ROOT, { recursive: true });

const { app, defaultResourceConfig } = require('../server');

const parsedBoards = yaml.load(fs.readFileSync(BOARDS_YAML, 'utf8'));
const esp32Board = parsedBoards.boards.esp32;

/* -------------------------------------------------------------------------
 * Desktop-shaped project fixture builders (mirrors project_store.cpp)
 * ---------------------------------------------------------------------- */
function desktopHardwareJson({ node0 = '', node1 = '', node2 = '', includeRx = true, resources = null, devices = [] } = {}) {
    const built = resources || defaultResourceConfig(esp32Board);
    const assignments = [
        { node: node0, role: 'tx', pin: 'GPIO1', resource: 'uart.uart0' }
    ];
    if (includeRx) assignments.push({ node: node1, role: 'rx', pin: 'GPIO3', resource: 'uart.uart0' });
    assignments.push({ node: node2, role: 'output', pin: 'GPIO25', resource: 'pwm.GPIO25' });
    return { version: 1, board: 'esp32', resources: built, assignments, devices };
}

function desktopClockJson() {
    return {
        version: 1,
        board: 'esp32',
        selections: { bbpll: '8', cpu_mux: 'bbpll', cpu_div: '2', uart_clk_sel: 'apb_clk', ledc_clk_sel: 'apb_clk' },
        resolved: {
            xtal: 40, rc_fast: 8, bbpll: 320, cpu_mux: 320, cpu_div: 160,
            apb_pll: 80, apb_clk: 80, ref_tick: 1,
            uart_clk_sel: 80, uart_clk: 80, ledc_clk_sel: 80, ledc_clk: 80,
            i2c_clk: 80, spi_clk: 80
        },
        errors: []
    };
}

function baseGraph(id, extraNodes = []) {
    return {
        format: 'hypraccel.mbd.graph', version: 1, id, name: id,
        nodes: [
            { id: 'level', type: 'Constant', params: { value: 1 }, position: { x: -100, y: 0 } },
            { id: 'pwm_out', type: 'PWMOutput', params: { hardwareResource: 'pwm.GPIO25', min: 0, max: 1 }, position: { x: 0, y: 0 } },
            ...extraNodes
        ],
        edges: [{ from: { node: 'level', port: 'value' }, to: { node: 'pwm_out', port: 'value' } }],
        metadata: { targetBoard: 'esp32' }
    };
}

function writeDesktopProject(id, { hardware = desktopHardwareJson(), clock = desktopClockJson(), graph = baseGraph('main') } = {}) {
    const projectDir = path.join(process.env.HYPRACCEL_PROJECTS_ROOT, id);
    fs.mkdirSync(path.join(projectDir, 'hardware'), { recursive: true });
    fs.mkdirSync(path.join(projectDir, 'graphs'), { recursive: true });
    const now = new Date().toISOString();
    const manifest = {
        format: 'hypraccel.project', version: 1, id, name: id,
        hardware: { path: 'hardware/hardware.json' },
        graphs: { path: 'graphs' },
        activeGraphId: graph.id,
        createdAt: now, updatedAt: now
    };
    fs.writeFileSync(path.join(projectDir, 'project.json'), JSON.stringify(manifest, null, 2) + '\n', 'utf8');
    fs.writeFileSync(path.join(projectDir, 'hardware', 'hardware.json'), JSON.stringify(hardware, null, 2) + '\n', 'utf8');
    if (clock) fs.writeFileSync(path.join(projectDir, 'hardware', 'clock.json'), JSON.stringify(clock, null, 2) + '\n', 'utf8');
    fs.writeFileSync(path.join(projectDir, 'graphs', `${graph.id}.json`), JSON.stringify(graph, null, 2) + '\n', 'utf8');
    return projectDir;
}

function request(server, method, requestPath, body) {
    return new Promise((resolve, reject) => {
        const payload = body == null ? null : JSON.stringify(body);
        const req = http.request({
            host: '127.0.0.1', port: server.address().port, path: requestPath, method,
            headers: payload ? { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) } : {}
        }, response => {
            let data = '';
            response.setEncoding('utf8');
            response.on('data', chunk => { data += chunk; });
            response.on('end', () => resolve({ status: response.statusCode, body: data ? JSON.parse(data) : null }));
        });
        req.on('error', reject);
        if (payload) req.write(payload);
        req.end();
    });
}

function clockPinBlock(headerText) {
    // Extract the clock/pin-function macro block for cross-comparison between
    // the CLI and the API/materialize path: everything from the first
    // HYP_CPU_FREQ_MHZ or HYP_CLOCK_ or HYP_PINFUNC_ line onward, excluding
    // the trailing #endif marker.
    return headerText
        .split('\n')
        .filter(line => /HYP_CPU_FREQ_MHZ|HYP_CLOCK_|HYP_PINFUNC_/.test(line))
        .join('\n');
}

async function run() {
    // POST /api/generate writes boards/codegen/hyp_board_config.h, a
    // checked-in file (scenario (d) below hits it).  Back it up and restore
    // it in `finally`, exactly like codegen_baseline/capture.js does.
    const checkedInHeader = path.join(REPO, 'boards/codegen/hyp_board_config.h');
    const checkedInBackup = fs.readFileSync(checkedInHeader);
    const server = await new Promise((resolve, reject) => {
        const instance = app.listen(0, '127.0.0.1', () => resolve(instance));
        instance.once('error', reject);
    });
    try {
        // (a) CLI --project output --------------------------------------
        const projectDirA = writeDesktopProject('desktop_a');
        const outDirA = fs.mkdtempSync(path.join(testRoot, 'cli-a-'));
        execFileSync(process.execPath, [GEN_BOARD_CONFIG, 'esp32', BOARDS_YAML, outDirA, '--project', projectDirA]);
        const headerA = fs.readFileSync(path.join(outDirA, 'hyp_board_config.h'), 'utf8');
        assert.match(headerA, /HYP_CPU_FREQ_MHZ\s+160\b/, 'CLI header must resolve CPU freq to 160 MHz');
        assert.match(headerA, /HYP_CLOCK_BEST_EFFORT/, 'CLI header must mark clock values as best-effort');
        assert.match(headerA, /HYP_PINFUNC_GPIO1\s+"uart\.uart0\.tx"/, 'CLI header must emit the GPIO1 pin function');
        assert.doesNotMatch(headerA, /HYP_PIN__/, 'CLI header must never emit an empty-node macro (HYP_PIN__*)');
        console.log('(a) CLI --project output: OK');

        // (b) POST /api/projects/<id>/generate ---------------------------
        const projectDirB = writeDesktopProject('desktop_b');
        let res = await request(server, 'POST', '/api/projects/desktop_b/generate', {});
        assert.equal(res.status, 200, `materialize: ${JSON.stringify(res.body)}`);
        const headerB = fs.readFileSync(path.join(projectDirB, 'generated', 'hyp_board_config.h'), 'utf8');
        assert.equal(clockPinBlock(headerB), clockPinBlock(headerA), 'materialize header must carry the same clock/pin-function block as the CLI');
        console.log('(b) POST /api/projects/:id/generate: OK');

        // (c) hardware.json on disk has no `clock` key and is otherwise unchanged
        const hardwareAfterB = JSON.parse(fs.readFileSync(path.join(projectDirB, 'hardware', 'hardware.json'), 'utf8'));
        assert.equal(Object.prototype.hasOwnProperty.call(hardwareAfterB, 'clock'), false, 'hardware.json must never gain a clock key');
        assert.equal(hardwareAfterB.board, 'esp32');
        assert.equal(hardwareAfterB.assignments.length, 3);
        console.log('(c) hardware.json unaffected by clock merge: OK');

        // (d) web-editor state (resource configuration / devices / node) is preserved
        const projectDirD = writeDesktopProject('desktop_d');
        let getRes = await request(server, 'GET', '/api/hardware?projectId=desktop_d');
        assert.equal(getRes.status, 200);
        const currentHardware = getRes.body;
        currentHardware.resources['uart.uart0'].configuration = { baudRate: 115200 };
        currentHardware.devices = [{ id: 'Dev1', name: 'Dev1', profile: '', connections: [
            { name: 'link', resource: 'uart.uart0', pin: 'GPIO1', role: 'tx' }
        ] }];
        const txAssignment = currentHardware.assignments.find(a => a.pin === 'GPIO1');
        txAssignment.node = 'SerialOut[0]';
        res = await request(server, 'PUT', '/api/projects/desktop_d/hardware', currentHardware);
        assert.equal(res.status, 200, `put hardware: ${JSON.stringify(res.body)}`);

        res = await request(server, 'POST', '/api/generate?projectId=desktop_d', {});
        assert.equal(res.status, 200, `api/generate: ${JSON.stringify(res.body)}`);
        assert.match(res.body.header, /HYP_RESOURCE_UART_UART0_BAUD_RATE\s+115200/);
        assert.match(res.body.header, /HYP_PIN_SERIALOUT_0_TX/);

        res = await request(server, 'POST', '/api/projects/desktop_d/generate', {});
        assert.equal(res.status, 200, `materialize: ${JSON.stringify(res.body)}`);
        const headerD = fs.readFileSync(path.join(projectDirD, 'generated', 'hyp_board_config.h'), 'utf8');
        assert.match(headerD, /HYP_RESOURCE_UART_UART0_BAUD_RATE\s+115200/);
        assert.match(headerD, /HYP_PIN_SERIALOUT_0_TX/);

        const hardwareAfterD = JSON.parse(fs.readFileSync(path.join(projectDirD, 'hardware', 'hardware.json'), 'utf8'));
        assert.deepEqual(hardwareAfterD.resources['uart.uart0'].configuration, { baudRate: 115200 });
        assert.equal(hardwareAfterD.devices.length, 1);
        assert.equal(hardwareAfterD.devices[0].id, 'Dev1');
        assert.equal(hardwareAfterD.assignments.find(a => a.pin === 'GPIO1').node, 'SerialOut[0]');
        console.log('(d) web-editor state preserved through generate/materialize: OK');

        // (e) conflict: GPIOInput on gpio.GPIO1 (already used by uart.uart0.tx)
        const conflictGraph = baseGraph('conflict_graph', [
            { id: 'gpio_in', type: 'GPIOInput', params: { hardwareResource: 'gpio.GPIO1' }, position: { x: 0, y: 100 } }
        ]);
        const projectDirE = writeDesktopProject('desktop_e', { graph: conflictGraph });
        assert.equal(fs.existsSync(path.join(projectDirE, 'generated')), false, 'generated/ must not pre-exist');
        res = await request(server, 'POST', '/api/projects/desktop_e/generate', {});
        assert.equal(res.status, 409, `expected 409 conflict: ${JSON.stringify(res.body)}`);
        assert.equal(fs.existsSync(path.join(projectDirE, 'generated')), false, 'generated/ must not be created on conflict');
        console.log('(e) pin conflict returns 409 and leaves generated/ untouched: OK');

        // (f) half-bus UART (tx only) succeeds with a warning
        const halfBusHardware = desktopHardwareJson({ includeRx: false });
        const projectDirF = writeDesktopProject('desktop_f', { hardware: halfBusHardware });
        res = await request(server, 'POST', '/api/projects/desktop_f/generate', {});
        assert.equal(res.status, 200, `half-bus materialize: ${JSON.stringify(res.body)}`);
        assert.ok(Array.isArray(res.body.generated.warnings) && res.body.generated.warnings.length > 0, 'half-bus UART must report a warning');
        const headerF = fs.readFileSync(path.join(projectDirF, 'generated', 'hyp_board_config.h'), 'utf8');
        assert.match(headerF, /warning/i, 'header must contain a warnings comment for the half-bus UART');
        console.log('(f) half-bus UART succeeds with a warning: OK');

        // (g) no clock.json -> no clock block
        const projectDirG = writeDesktopProject('desktop_g', { clock: null });
        assert.equal(fs.existsSync(path.join(projectDirG, 'hardware', 'clock.json')), false);
        res = await request(server, 'POST', '/api/projects/desktop_g/generate', {});
        assert.equal(res.status, 200, `no-clock materialize: ${JSON.stringify(res.body)}`);
        const headerG = fs.readFileSync(path.join(projectDirG, 'generated', 'hyp_board_config.h'), 'utf8');
        assert.doesNotMatch(headerG, /HYP_CPU_FREQ_MHZ/, 'no clock.json must mean no clock block');
        assert.doesNotMatch(headerG, /HYP_CLOCK_/, 'no clock.json must mean no clock block');
        console.log('(g) no clock.json -> no clock block: OK');

        console.log('desktop_codegen.test.js: all scenarios passed');
    } finally {
        server.close();
        fs.writeFileSync(checkedInHeader, checkedInBackup);
    }
}

run()
    .catch(err => { console.error(err); process.exitCode = 1; })
    .finally(() => { fs.rmSync(testRoot, { recursive: true, force: true }); });
