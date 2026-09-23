'use strict';

/*
 * codegen_regression.test.js — golden-file regression for the refactored
 * codegen wiring (project_config.js / project_defines.js / pin_conflicts.js).
 * Reproduces exactly what mbd/editor/test/fixtures/codegen_baseline/capture.js
 * captured for web-editor-only projects (no clock.json) and asserts the
 * refactored code path still produces byte-identical output.
 *
 * IMPORTANT: POST /api/generate writes boards/codegen/hyp_board_config.h, a
 * checked-in file.  We back it up and restore it in `finally`, exactly like
 * capture.js does.  This test never touches the real .hypraccel/projects.
 */
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const { execFileSync } = require('child_process');

const REPO = path.resolve(__dirname, '../../..');
const FIXTURES = path.join(__dirname, 'fixtures/codegen_baseline');

const testRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-codegen-regression-'));
process.env.HYPRACCEL_PROJECTS_ROOT = path.join(testRoot, 'projects');
process.env.HYPRACCEL_HARDWARE_CONFIG = path.join(testRoot, 'legacy-hardware.json');

fs.mkdirSync(path.join(testRoot, 'projects'), { recursive: true });
fs.cpSync(path.join(REPO, '.hypraccel/projects/demo_project'), path.join(testRoot, 'projects/demo_project'), { recursive: true });
fs.rmSync(path.join(testRoot, 'projects/demo_project/generated'), { recursive: true, force: true });

const { app } = require('../server');

const graph = {
    format: 'hypraccel.mbd.graph', version: 1, id: 'uart_graph', name: 'UART Graph',
    nodes: [{ id: 'constant', type: 'Constant', params: { value: 1 }, position: { x: 0, y: 0 } }],
    edges: [], metadata: { targetBoard: 'esp32' }
};
const hardware = {
    board: 'esp32',
    assignments: [
        { node: 'SerialLink[0]', role: 'tx', pin: 'GPIO17', resource: 'uart.uart2' },
        { node: 'SerialLink[0]', role: 'rx', pin: 'GPIO16', resource: 'uart.uart2' },
        { node: 'Servo[0]', role: 'output', pin: 'GPIO25', resource: 'pwm.GPIO25' }
    ],
    configurations: { 'uart.uart2': { baudRate: 57600 } }
};

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

function assertMatchesFixture(actualPath, fixtureName) {
    const actual = fs.readFileSync(actualPath, 'utf8');
    const expected = fs.readFileSync(path.join(FIXTURES, fixtureName), 'utf8');
    assert.equal(actual, expected, `${actualPath} must byte-match fixtures/codegen_baseline/${fixtureName}`);
}

async function run() {
    // --- CLI: esp32 / thejas32, byte-identical to cli_<board>.h -----------
    for (const board of ['esp32', 'thejas32']) {
        const dir = fs.mkdtempSync(path.join(testRoot, 'cli-'));
        execFileSync(process.execPath, [path.join(REPO, 'boards/codegen/gen_board_config.js'), board, path.join(REPO, 'boards/boards.yaml'), dir]);
        assertMatchesFixture(path.join(dir, 'hyp_board_config.h'), `cli_${board}.h`);
    }

    const checkedIn = path.join(REPO, 'boards/codegen/hyp_board_config.h');
    const backup = fs.readFileSync(checkedIn);
    const server = await new Promise((resolve, reject) => {
        const instance = app.listen(0, '127.0.0.1', () => resolve(instance));
        instance.once('error', reject);
    });
    try {
        let res = await request(server, 'POST', '/api/projects', { id: 'web_uart', name: 'Web UART', hardware, graph });
        assert.equal(res.status, 201, `create web_uart: ${JSON.stringify(res.body)}`);

        for (const id of ['demo_project', 'web_uart']) {
            res = await request(server, 'POST', `/api/projects/${id}/generate`, {});
            assert.equal(res.status, 200, `${id} materialize: ${JSON.stringify(res.body)}`);
            assertMatchesFixture(
                path.join(process.env.HYPRACCEL_PROJECTS_ROOT, id, 'generated/hyp_board_config.h'),
                `materialize_${id}.h`
            );

            res = await request(server, 'POST', `/api/generate?projectId=${id}`, {});
            assert.equal(res.status, 200, `${id} api/generate: ${JSON.stringify(res.body)}`);
            assertMatchesFixture(checkedIn, `api_generate_${id}.h`);
        }
    } finally {
        server.close();
        fs.writeFileSync(checkedIn, backup);
    }

    console.log('codegen_regression.test.js: all golden outputs matched');
}

run()
    .catch(err => { console.error(err); process.exitCode = 1; })
    .finally(() => { fs.rmSync(testRoot, { recursive: true, force: true }); });
