'use strict';

/* Focused API regression for persistent project storage.  No third-party test
 * harness is needed; keeping it in Node makes it runnable in the editor's
 * existing dependency set. */
const assert = require('assert');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const testRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-project-api-'));
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
            response.on('end', () => resolve({
                status: response.statusCode,
                body: data ? JSON.parse(data) : null
            }));
        });
        req.on('error', reject);
        if (payload) req.write(payload);
        req.end();
    });
}

const graph = {
    format: 'hypraccel.mbd.graph', version: 1, id: 'workspace_graph', name: 'Workspace Graph',
    nodes: [{ id: 'constant', type: 'Constant', params: { value: 1 }, position: { x: 0, y: 0 } }],
    edges: [], metadata: {}
};
const hardware = { board: 'esp32', assignments: [] };

async function run() {
    const server = await new Promise((resolve, reject) => {
        const instance = app.listen(0, '127.0.0.1', () => resolve(instance));
        instance.once('error', reject);
    });
    try {
        let response = await request(server, 'GET', '/api/projects');
        assert.equal(response.status, 200);
        assert.deepEqual(response.body.projects, []);

        response = await request(server, 'POST', '/api/projects', {
            id: 'workspace_demo', name: 'Workspace Demo', hardware, graph
        });
        assert.equal(response.status, 201);
        assert.equal(response.body.project.board, 'esp32');
        assert.equal(response.body.project.graphId, 'workspace_graph');

        const projectDir = path.join(process.env.HYPRACCEL_PROJECTS_ROOT, 'workspace_demo');
        const manifest = JSON.parse(fs.readFileSync(path.join(projectDir, 'project.json'), 'utf8'));
        assert.equal(manifest.hardware.path, 'hardware/hardware.json');
        assert.equal(manifest.graph.path, 'graph/graph.json');
        assert.equal(Object.hasOwn(manifest, 'board'), false, 'board must remain canonical in hardware.json');
        assert.equal(Object.hasOwn(manifest, 'graphId'), false, 'graph id must remain canonical in graph.json');
        assert.equal(JSON.parse(fs.readFileSync(path.join(projectDir, 'hardware', 'hardware.json'), 'utf8')).board, 'esp32');
        assert.equal(JSON.parse(fs.readFileSync(path.join(projectDir, 'graph', 'graph.json'), 'utf8')).id, 'workspace_graph');

        response = await request(server, 'GET', '/api/projects/workspace_demo');
        assert.equal(response.status, 200);
        assert.equal(response.body.name, 'Workspace Demo');
        assert.equal(response.body.hardware.board, 'esp32');
        assert.equal(response.body.graph.id, 'workspace_graph');

        response = await request(server, 'PUT', '/api/projects/workspace_demo/hardware', hardware);
        assert.equal(response.status, 200);
        assert.equal(response.body.hardware.board, 'esp32');
        response = await request(server, 'PUT', '/api/projects/workspace_demo/graph', graph);
        assert.equal(response.status, 200);
        assert.equal(response.body.graph.id, 'workspace_graph');

        response = await request(server, 'GET', '/api/projects/workspace_demo/status');
        assert.equal(response.status, 200);
        assert.deepEqual(response.body.generated, []);
        assert.equal(response.body.build.log, null);

        response = await request(server, 'PUT', '/api/projects/workspace_demo', { name: 'Renamed Workspace' });
        assert.equal(response.status, 200);
        assert.equal(response.body.project.name, 'Renamed Workspace');

        response = await request(server, 'GET', '/api/projects');
        assert.equal(response.status, 200);
        assert.equal(response.body.projects.length, 1);
        assert.equal(response.body.projects[0].id, 'workspace_demo');

        response = await request(server, 'GET', '/api/hardware?projectId=workspace_demo');
        assert.equal(response.status, 200);
        assert.equal(response.body.board, 'esp32');

        response = await request(server, 'GET', '/api/hardware');
        assert.equal(response.status, 200, 'existing unscoped /api/hardware remains available');
        assert.equal(response.body.board, null);

        response = await request(server, 'POST', '/api/build', graph);
        assert.equal(response.status, 200, 'existing graph generation remains available');
        assert.match(response.body.source, /hyp_graph_workspace_graph_step/);

        response = await request(server, 'GET', '/api/projects/workspace_demo/source?path=graph/graph.json');
        assert.equal(response.status, 200);
        assert.match(response.body.content, /workspace_graph/);

        response = await request(server, 'GET', '/api/projects/workspace_demo/source?path=../project.json');
        assert.equal(response.status, 400);
        response = await request(server, 'POST', '/api/projects', { id: '../escape', name: 'Bad' });
        assert.equal(response.status, 400);
        response = await request(server, 'GET', '/api/projects/bad.id');
        assert.equal(response.status, 400);

        response = await request(server, 'DELETE', '/api/projects/workspace_demo');
        assert.equal(response.status, 200);
        assert.equal(fs.existsSync(projectDir), false);
        response = await request(server, 'GET', '/api/projects');
        assert.deepEqual(response.body.projects, []);
    } finally {
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(testRoot, { recursive: true, force: true });
    }
}

run().then(() => console.log('project_api.test.js: PASS')).catch(err => {
    console.error(err.stack || err);
    process.exitCode = 1;
});
