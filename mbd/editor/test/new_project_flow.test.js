'use strict';

/* End-to-end smoke test for the MBD editor "New Project" flow: board
 * discovery, project creation (board-only, with hardware, with a starter
 * graph), validation/rejection paths, orphaned-state hygiene, cleanup, and
 * the static front-end wiring (workspace.html + board_detail.js) that the
 * "New Project" dialog depends on.
 *
 * Run each check as a named step so a single missing piece (this test is
 * written against a contract other agents are implementing in parallel in
 * server.js / workspace.html / board_detail.js) doesn't hide the status of
 * every other step.  All steps run; failures are collected and reported at
 * the end. */
const assert = require('assert');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const testRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-new-project-flow-'));
process.env.HYPRACCEL_PROJECTS_ROOT = path.join(testRoot, 'projects');
process.env.HYPRACCEL_HARDWARE_CONFIG = path.join(testRoot, 'legacy-hardware.json');

const { app } = require('../server');

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
            response.on('end', () => {
                const contentType = response.headers['content-type'] || '';
                let parsed = null;
                if (data && contentType.includes('application/json')) {
                    try { parsed = JSON.parse(data); } catch (_err) { parsed = data; }
                } else {
                    parsed = data;
                }
                resolve({ status: response.statusCode, headers: response.headers, body: parsed });
            });
        });
        req.on('error', reject);
        if (payload) req.write(payload);
        req.end();
    });
}

const results = [];

async function step(name, fn) {
    try {
        await fn();
        results.push({ name, pass: true });
    } catch (err) {
        results.push({ name, pass: false, error: err });
    }
}

function starterGraph(id, board) {
    return {
        format: 'hypraccel.mbd.graph', version: 1, id: `${id}_graph`, name: 'X Graph',
        nodes: [{ id: 'constant', type: 'Constant', params: { value: 1 }, position: { x: 100, y: 100 } }],
        edges: [], metadata: { targetBoard: board }
    };
}

async function run() {
    const server = await new Promise((resolve, reject) => {
        const instance = app.listen(0, '127.0.0.1', () => resolve(instance));
        instance.once('error', reject);
    });

    const projectsRoot = process.env.HYPRACCEL_PROJECTS_ROOT;
    // Every project id this run has successfully created (201), used for the
    // orphaned-state check and final cleanup. Populated regardless of
    // whether a given step *expected* success, so the orphan check stays
    // correct even if a should-be-rejected request unexpectedly succeeds
    // while the contract is still landing.
    const createdIds = [];

    async function post(body) {
        const response = await request(server, 'POST', '/api/projects', body);
        if (response.status >= 200 && response.status < 300) {
            const id = (response.body && response.body.project && response.body.project.id) || body.id;
            if (id) createdIds.push(id);
        }
        return response;
    }

    async function assertNoDirCreated(id) {
        const dir = path.join(projectsRoot, id);
        assert.equal(fs.existsSync(dir), false, `directory for rejected project '${id}' must not exist`);
    }

    try {
        await step('GET /api/boards includes esp32 and thejas32 with names', async () => {
            const response = await request(server, 'GET', '/api/boards');
            assert.equal(response.status, 200);
            assert.equal(typeof response.body, 'object');
            for (const key of ['esp32', 'thejas32']) {
                assert.ok(response.body[key], `boards map must include '${key}'`);
                assert.equal(typeof response.body[key].name, 'string');
                assert.ok(response.body[key].name.length > 0);
            }
        });

        for (const board of ['esp32', 'thejas32']) {
            await step(`POST /api/projects board-only (${board}) creates a well-formed project`, async () => {
                const id = `newflow_${board}_only`;
                const response = await post({ id, name: `${board} Only Project`, board });
                assert.equal(response.status, 201, `expected 201, got ${response.status}: ${JSON.stringify(response.body)}`);
                const project = response.body.project;
                assert.equal(project.id, id);
                assert.equal(project.name, `${board} Only Project`);
                assert.equal(project.board, board, 'project.board must equal the requested board');
                assert.ok(project.hardware, 'project.hardware must be present');
                assert.equal(project.hardware.board, board);
                assert.ok(project.hardware.resources && project.hardware.resources['accelerator.cordic'],
                    "project.hardware.resources['accelerator.cordic'] must be present");

                const projectDir = path.join(projectsRoot, id);
                const manifest = JSON.parse(fs.readFileSync(path.join(projectDir, 'project.json'), 'utf8'));
                assert.equal(manifest.format, 'hypraccel.project');
                assert.equal(manifest.version, 1);
                assert.equal(manifest.id, id);
                assert.equal(Object.prototype.hasOwnProperty.call(manifest, 'board'), false,
                    'project.json must not carry a board key; hardware.json is canonical');

                const hardwareOnDisk = JSON.parse(fs.readFileSync(path.join(projectDir, 'hardware', 'hardware.json'), 'utf8'));
                assert.equal(hardwareOnDisk.board, board);

                assert.ok(fs.existsSync(path.join(projectDir, 'graphs')) && fs.statSync(path.join(projectDir, 'graphs')).isDirectory(),
                    'graphs/ directory must exist even with no starter graph');
            });
        }

        await step('POST /api/projects with starter graph sets activeGraphId and is retrievable', async () => {
            const id = 'newflow_with_graph';
            const graph = starterGraph(id, 'esp32');
            const response = await post({ id, name: 'With Graph Project', board: 'esp32', graph });
            assert.equal(response.status, 201, `expected 201, got ${response.status}: ${JSON.stringify(response.body)}`);
            assert.equal(response.body.project.activeGraphId, `${id}_graph`);

            const getResponse = await request(server, 'GET', `/api/projects/${id}`);
            assert.equal(getResponse.status, 200);
            assert.equal(getResponse.body.graph.id, `${id}_graph`);

            const listResponse = await request(server, 'GET', '/api/projects');
            assert.equal(listResponse.status, 200);
            assert.ok(listResponse.body.projects.some(p => p.id === id), 'project list must include the new project');
        });

        await step("POST /api/projects with unknown board 'nope' is rejected without creating a directory", async () => {
            const id = 'newflow_unknown_board';
            const response = await post({ id, name: 'Unknown Board', board: 'nope' });
            assert.equal(response.status, 400, `expected 400, got ${response.status}: ${JSON.stringify(response.body)}`);
            await assertNoDirCreated(id);
        });

        await step("POST /api/projects with prototype key board 'toString' is rejected without creating a directory", async () => {
            const id = 'newflow_proto_board';
            const response = await post({ id, name: 'Proto Board', board: 'toString' });
            assert.equal(response.status, 400, `expected 400, got ${response.status}: ${JSON.stringify(response.body)}`);
            await assertNoDirCreated(id);
        });

        await step('POST /api/projects with mismatched board/hardware.board is rejected without creating a directory', async () => {
            const id = 'newflow_mismatched_board';
            const response = await post({ id, name: 'Mismatched Board', board: 'esp32', hardware: { board: 'thejas32', assignments: [] } });
            assert.equal(response.status, 400, `expected 400, got ${response.status}: ${JSON.stringify(response.body)}`);
            await assertNoDirCreated(id);
        });

        await step('POST /api/projects with invalid id is rejected without creating a directory', async () => {
            const id = '1bad';
            const response = await post({ id, name: 'Invalid Id', board: 'esp32' });
            assert.equal(response.status, 400, `expected 400, got ${response.status}: ${JSON.stringify(response.body)}`);
            await assertNoDirCreated(id);
        });

        await step('POST /api/projects with empty name is rejected without creating a directory', async () => {
            const id = 'newflow_empty_name';
            const response = await post({ id, name: '', board: 'esp32' });
            assert.equal(response.status, 400, `expected 400, got ${response.status}: ${JSON.stringify(response.body)}`);
            await assertNoDirCreated(id);
        });

        await step('POST /api/projects with duplicate id is rejected (409) and leaves the original untouched', async () => {
            const id = 'newflow_esp32_only'; // created above
            const projectJsonPath = path.join(projectsRoot, id, 'project.json');
            const before = fs.readFileSync(projectJsonPath);
            const response = await post({ id, name: 'Duplicate Attempt', board: 'esp32' });
            assert.equal(response.status, 409, `expected 409, got ${response.status}: ${JSON.stringify(response.body)}`);
            const after = fs.readFileSync(projectJsonPath);
            assert.ok(before.equals(after), 'project.json must be byte-identical after a rejected duplicate create');
        });

        await step('legacy body with neither board nor hardware still creates a project (back-compat)', async () => {
            const id = 'newflow_legacy_body';
            const response = await post({ id, name: 'Legacy Body Project' });
            assert.equal(response.status, 201, `expected 201, got ${response.status}: ${JSON.stringify(response.body)}`);
        });

        await step('no orphaned state: projects root contains exactly the successfully created project directories', async () => {
            const onDisk = fs.existsSync(projectsRoot)
                ? fs.readdirSync(projectsRoot, { withFileTypes: true }).filter(e => e.isDirectory()).map(e => e.name).sort()
                : [];
            const expected = [...new Set(createdIds)].sort();
            assert.deepEqual(onDisk, expected, `projects root must contain exactly the created projects.\non disk: ${JSON.stringify(onDisk)}\nexpected: ${JSON.stringify(expected)}`);
        });

        await step('cleanup: DELETE each created project removes its directory', async () => {
            for (const id of new Set(createdIds)) {
                const response = await request(server, 'DELETE', `/api/projects/${id}`);
                assert.equal(response.status, 200, `DELETE /api/projects/${id} expected 200, got ${response.status}`);
                assert.equal(fs.existsSync(path.join(projectsRoot, id)), false, `directory for '${id}' must be gone after delete`);
            }
        });

        await step('GET /board_detail.js serves the HyprBoardDetail front-end module', async () => {
            const response = await request(server, 'GET', '/board_detail.js');
            assert.equal(response.status, 200, `expected 200, got ${response.status}`);
            assert.equal(typeof response.body, 'string', 'response body must be returned as text, not JSON');
            assert.match(response.body, /HyprBoardDetail/);
        });

        await step('GET /workspace serves HTML wired for board selection + board detail + new-project error', async () => {
            const response = await request(server, 'GET', '/workspace');
            assert.equal(response.status, 200, `expected 200, got ${response.status}`);
            assert.equal(typeof response.body, 'string', 'response body must be returned as text, not JSON');
            assert.match(response.body, /id="inputProjBoard"/);
            assert.match(response.body, /id="boardDetailPanel"/);
            assert.match(response.body, /board_detail\.js/);
            assert.match(response.body, /id="newProjectError"/);
        });
    } finally {
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(testRoot, { recursive: true, force: true });
    }
}

run().then(() => {
    const passed = results.filter(r => r.pass);
    const failed = results.filter(r => !r.pass);
    console.log('\nnew_project_flow.test.js — step summary');
    console.log('='.repeat(60));
    for (const r of results) {
        console.log(`[${r.pass ? 'PASS' : 'FAIL'}] ${r.name}`);
        if (!r.pass) console.log(`       ${r.error.message || r.error}`);
    }
    console.log('='.repeat(60));
    console.log(`${passed.length}/${results.length} steps passed, ${failed.length} failed.`);
    if (failed.length > 0) {
        console.log('\nnew_project_flow.test.js: FAIL');
        process.exitCode = 1;
    } else {
        console.log('\nnew_project_flow.test.js: PASS');
    }
}).catch(err => {
    console.error('new_project_flow.test.js: harness error (outside of any step)');
    console.error(err.stack || err);
    process.exitCode = 1;
});
