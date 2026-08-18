'use strict';

/* Round-trip coverage for the graph editor's persistent project contract. */
const assert = require('assert');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const testRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-graph-persistence-'));
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
            response.on('end', () => resolve({ status: response.statusCode, body: data ? JSON.parse(data) : null }));
        });
        req.on('error', reject);
        if (payload) req.write(payload);
        req.end();
    });
}

const hardware = {
    board: 'esp32',
    assignments: [{ node: 'gpio_input', role: 'gpio', pin: 'GPIO0', resource: 'gpio.GPIO0' }]
};
const graph = {
    format: 'hypraccel.mbd.graph', version: 1, id: 'roundtrip_graph', name: 'Round Trip Graph',
    nodes: [
        { id: 'constant_1', type: 'Constant', label: 'Constant constant_1', params: { value: 2.5 }, position: { x: 10, y: 20 } },
        { id: 'time_1', type: 'Time', label: 'Time time_1', params: {}, position: { x: 180, y: 20 } },
        { id: 'cordic_1', type: 'CordicOp', label: 'CORDIC cordic_1', params: { operation: 'sincos', implementation: 'hardware', iterations: 12, hardwareResource: 'accelerator.cordic' }, position: { x: 350, y: 20 } },
        { id: 'gpio_input', type: 'GPIOInput', label: 'GPIO Input gpio_input', params: { hardwareResource: 'gpio.GPIO0', samplePeriodUs: 500, invert: true }, position: { x: 10, y: 180 } },
        { id: 'custom_1', type: 'CustomCode', label: 'Custom Code custom_1', params: { inputs: ['value', 'elapsed'], code: 'return value + elapsed;' }, position: { x: 520, y: 20 } }
    ],
    edges: [
        { id: 'constant-to-cordic', from: { node: 'constant_1', port: 'value' }, to: { node: 'cordic_1', port: 'angle_rad' } },
        { id: 'constant-to-custom', from: { node: 'constant_1', port: 'value' }, to: { node: 'custom_1', port: 'value' } },
        { id: 'time-to-custom', from: { node: 'time_1', port: 'value' }, to: { node: 'custom_1', port: 'elapsed' } }
    ],
    inputs: [{ name: 'angle_rad', type: 'number', unit: 'rad' }],
    metadata: {
        targetBoard: 'esp32',
        externalBindings: [{ input: 'angle_rad', to: { node: 'cordic_1', port: 'angle_rad' } }]
    }
};

async function run() {
    const server = await new Promise((resolve, reject) => {
        const instance = app.listen(0, '127.0.0.1', () => resolve(instance));
        instance.once('error', reject);
    });
    try {
        let response = await request(server, 'POST', '/api/projects', { id: 'roundtrip_project', name: 'Round Trip', hardware });
        assert.equal(response.status, 201);
        response = await request(server, 'PUT', '/api/projects/roundtrip_project/graph', graph);
        assert.equal(response.status, 200, JSON.stringify(response.body));
        assert.deepEqual(response.body.graph, graph);

        response = await request(server, 'GET', '/api/projects/roundtrip_project/graph');
        assert.equal(response.status, 200);
        assert.deepEqual(response.body, graph);
        assert.deepEqual(response.body.nodes, graph.nodes);
        assert.deepEqual(response.body.edges, graph.edges);
        assert.equal(response.body.nodes.find(node => node.type === 'Time').params && typeof response.body.nodes.find(node => node.type === 'Time').params, 'object');
        assert.equal(response.body.nodes.find(node => node.type === 'GPIOInput').params.hardwareResource, 'gpio.GPIO0');
        assert.deepEqual(response.body.nodes.find(node => node.type === 'CustomCode').params, graph.nodes[4].params);
        assert.deepEqual(response.body.nodes.find(node => node.type === 'CordicOp').params, graph.nodes[2].params);

        response = await request(server, 'POST', '/api/projects/roundtrip_project/generate');
        assert.equal(response.status, 200, JSON.stringify(response.body));
        const graphPath = path.join(process.env.HYPRACCEL_PROJECTS_ROOT, 'roundtrip_project', 'generated', 'graph.c');
        const firstSource = fs.readFileSync(graphPath, 'utf8');
        response = await request(server, 'POST', '/api/projects/roundtrip_project/generate');
        assert.equal(response.status, 200);
        assert.equal(fs.readFileSync(graphPath, 'utf8'), firstSource, 'generation after reload must be deterministic');
        assert.match(firstSource, /hyp_graph_roundtrip_graph_step/);
    } finally {
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(testRoot, { recursive: true, force: true });
    }
}

run().then(() => console.log('graph_persistence.test.js: PASS')).catch(error => {
    console.error(error.stack || error);
    process.exitCode = 1;
});
