'use strict';

const assert = require('assert');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const testRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-materialization-'));
process.env.HYPRACCEL_PROJECTS_ROOT = path.join(testRoot, 'projects');
process.env.HYPRACCEL_HARDWARE_CONFIG = path.join(testRoot, 'global-hardware.json');
const fakePlatformio = path.join(testRoot, 'fake-pio.sh');
fs.writeFileSync(fakePlatformio, '#!/bin/sh\nproject=""\nprevious=""\nfor arg in "$@"; do\n  if [ "$previous" = "--project-dir" ]; then project="$arg"; fi\n  previous="$arg"\ndone\nmkdir -p "$project/.pio/build/esp32dev"\nprintf "fake firmware" > "$project/.pio/build/esp32dev/firmware.bin"\nprintf "fake platformio build for %s\\n" "$project"\n', 'utf8');
fs.chmodSync(fakePlatformio, 0o755);
if (!process.env.HYPRACCEL_TEST_REAL_PIO) process.env.HYPRACCEL_PLATFORMIO = fakePlatformio;

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
            response.on('end', () => resolve({ status: response.statusCode, body: data }));
        });
        req.on('error', reject);
        if (payload) req.write(payload);
        req.end();
    });
}

const graph = {
    format: 'hypraccel.mbd.graph', version: 1, id: 'materialized_graph', name: 'Materialized Graph',
    nodes: [{ id: 'constant', type: 'Constant', params: { value: 1 }, position: { x: 0, y: 0 } }],
    edges: [], metadata: {}
};
const hardware = { board: 'esp32', assignments: [] };

async function run() {
    const server = await new Promise((resolve, reject) => {
        const instance = app.listen(0, '127.0.0.1', () => resolve(instance));
        instance.once('error', reject);
    });
    const projectDir = path.join(process.env.HYPRACCEL_PROJECTS_ROOT, 'project_a');
    try {
        let response = await request(server, 'POST', '/api/projects', { id: 'project_a', name: 'Project A', hardware, graph });
        assert.equal(response.status, 201);

        response = await request(server, 'POST', '/api/projects/project_a/graphs/materialized_graph/generate');
        assert.equal(response.status, 200);
        for (const file of ['graph.c', 'main.cpp', 'hyp_board_config.h', 'hyprccel.h', 'hyp_esp32_hw.h']) {
            assert.equal(fs.existsSync(path.join(projectDir, 'generated', file)), true, `${file} was not materialized`);
        }
        const platformio = fs.readFileSync(path.join(projectDir, 'platformio.ini'), 'utf8');
        assert.match(platformio, /board = esp32dev/);
        assert.match(platformio, /src_dir = generated/);

        // Change the global/current hardware state after generation. Project A
        // must continue using its own persisted ESP32 hardware.json.
        response = await request(server, 'POST', '/api/hardware', { board: 'thejas32', assignments: [] });
        assert.equal(response.status, 200);
        response = await request(server, 'POST', '/api/projects/project_a/graphs/materialized_graph/compile');
        if (response.status !== 200) console.error('real PlatformIO compile response:', response.body);
        assert.equal(response.status, 200);
        if (!process.env.HYPRACCEL_TEST_REAL_PIO) assert.match(response.body, /fake platformio build/);
        assert.equal(fs.existsSync(path.join(projectDir, 'build', 'build.log')), true);
        assert.equal(fs.existsSync(path.join(projectDir, 'build', 'esp32dev', 'firmware.bin')), true);
        if (!process.env.HYPRACCEL_TEST_REAL_PIO) {
            assert.equal(fs.readFileSync(path.join(projectDir, 'build', 'esp32dev', 'firmware.bin'), 'utf8'), 'fake firmware');
        }
        assert.equal(fs.readFileSync(path.join(projectDir, 'hardware', 'hardware.json'), 'utf8').includes('"board": "esp32"'), true);

        // Reload and compile again from disk, with no client-supplied graph.
        response = await request(server, 'POST', '/api/compile?projectId=project_a&graphId=materialized_graph');
        assert.equal(response.status, 200);
        response = await request(server, 'GET', '/api/projects/project_a/status');
        assert.equal(response.status, 200);
        const status = JSON.parse(response.body);
        assert.equal(status.build.log, 'build/build.log');
        assert.deepEqual(status.build.environments, ['esp32dev']);
        assert.equal(status.graphArtifacts.generatedGraphId, 'materialized_graph');
        assert.equal(status.graphArtifacts.buildGraphId, 'materialized_graph');

        response = await request(server, 'GET', '/api/projects/project_a/source?path=generated/main.cpp');
        assert.equal(response.status, 200);
        const mainSource = JSON.parse(response.body).content;
        assert.match(mainSource, /HYPRACCEL_GRAPH_ID=/);
        assert.match(mainSource, /materialized_graph/);
        response = await request(server, 'GET', '/api/projects/project_a/source?path=../project.json');
        assert.equal(response.status, 400);
        response = await request(server, 'GET', '/api/projects/project_b/source?path=generated/main.cpp');
        assert.equal(response.status, 404);
        response = await request(server, 'POST', '/api/projects/%2e%2e/generate');
        assert.equal(response.status, 400);
    } finally {
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(testRoot, { recursive: true, force: true });
    }
}

run().then(() => console.log('project_materialization.test.js: PASS')).catch(err => {
    console.error(err.stack || err);
    process.exitCode = 1;
});
