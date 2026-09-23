// Captures codegen output for web-editor-only projects (no clock.json).
// Usage: node codegen_baseline.js <outDir>
'use strict';
const fs = require('fs'), os = require('os'), path = require('path'), http = require('http');
const { execFileSync } = require('child_process');
const REPO = path.resolve(__dirname, '../../../..');
const outDir = path.resolve(process.argv[2]);
fs.mkdirSync(outDir, { recursive: true });
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-baseline-'));
process.env.HYPRACCEL_PROJECTS_ROOT = path.join(root, 'projects');
process.env.HYPRACCEL_HARDWARE_CONFIG = path.join(root, 'legacy-hardware.json');
fs.cpSync(path.join(REPO, '.hypraccel/projects/demo_project'), path.join(root, 'projects/demo_project'), { recursive: true });
fs.rmSync(path.join(root, 'projects/demo_project/generated'), { recursive: true, force: true });
const { app } = require(path.join(REPO, 'mbd/editor/server'));
const graph = { format: 'hypraccel.mbd.graph', version: 1, id: 'uart_graph', name: 'UART Graph',
    nodes: [{ id: 'constant', type: 'Constant', params: { value: 1 }, position: { x: 0, y: 0 } }], edges: [], metadata: { targetBoard: 'esp32' } };
const hardware = { board: 'esp32', assignments: [
    { node: 'SerialLink[0]', role: 'tx', pin: 'GPIO17', resource: 'uart.uart2' },
    { node: 'SerialLink[0]', role: 'rx', pin: 'GPIO16', resource: 'uart.uart2' },
    { node: 'Servo[0]', role: 'output', pin: 'GPIO25', resource: 'pwm.GPIO25' }],
    configurations: { 'uart.uart2': { baudRate: 57600 } } };
function request(server, method, p, body) {
    return new Promise((resolve, reject) => {
        const payload = body == null ? null : JSON.stringify(body);
        const req = http.request({ host: '127.0.0.1', port: server.address().port, path: p, method,
            headers: payload ? { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) } : {} }, r => {
            let d = ''; r.setEncoding('utf8'); r.on('data', c => d += c); r.on('end', () => resolve({ status: r.statusCode, body: d ? JSON.parse(d) : null }));
        });
        req.on('error', reject); if (payload) req.write(payload); req.end();
    });
}
(async () => {
    for (const board of ['esp32', 'thejas32']) {
        const dir = fs.mkdtempSync(path.join(root, 'cli-'));
        execFileSync(process.execPath, [path.join(REPO, 'boards/codegen/gen_board_config.js'), board, path.join(REPO, 'boards/boards.yaml'), dir]);
        fs.copyFileSync(path.join(dir, 'hyp_board_config.h'), path.join(outDir, `cli_${board}.h`));
    }
    const checkedIn = path.join(REPO, 'boards/codegen/hyp_board_config.h');
    const backup = fs.readFileSync(checkedIn);
    const server = await new Promise(r => { const s = app.listen(0, '127.0.0.1', () => r(s)); });
    try {
        let res = await request(server, 'POST', '/api/projects', { id: 'web_uart', name: 'Web UART', hardware, graph });
        if (res.status !== 201) throw new Error('create: ' + JSON.stringify(res.body));
        for (const id of ['demo_project', 'web_uart']) {
            res = await request(server, 'POST', `/api/projects/${id}/generate`, {});
            if (res.status !== 200) throw new Error(`${id} generate: ${JSON.stringify(res.body)}`);
            fs.copyFileSync(path.join(root, 'projects', id, 'generated/hyp_board_config.h'), path.join(outDir, `materialize_${id}.h`));
            res = await request(server, 'POST', `/api/generate?projectId=${id}`, {});
            if (res.status !== 200) throw new Error(`${id} api/generate: ${JSON.stringify(res.body)}`);
            fs.copyFileSync(checkedIn, path.join(outDir, `api_generate_${id}.h`));
            fs.writeFileSync(path.join(outDir, `api_generate_${id}.response.json`), JSON.stringify(res.body, null, 2));
        }
    } finally {
        server.close(); fs.writeFileSync(checkedIn, backup); fs.rmSync(root, { recursive: true, force: true });
    }
    console.log('baseline written to', outDir, fs.readdirSync(outDir).join(' '));
})().catch(e => { console.error(e); process.exit(1); });
