'use strict';

/*
 * check_pin_conflicts.test.js — covers:
 *   (1) boards/codegen/check_pin_conflicts.js: stdin -> stdout CLI, reusing
 *       the SAME checkPinConflicts()/mergeProjectConfig() chain codegen and
 *       the web editor use. Cases: I2C SDA-only (BUS_INCOMPLETE error), UART
 *       TX-only (BUS_PARTIAL warning, zero errors), malformed stdin (exit 2).
 *   (2) POST /api/hardware/check on the editor server: same two cases, plus
 *       proof the request writes nothing (project dir mtimes/contents
 *       unchanged, checked-in boards/codegen/hyp_board_config.h unchanged).
 *
 * Follows the style of desktop_codegen.test.js: temp HYPRACCEL_PROJECTS_ROOT,
 * a real HTTP server, restore-in-finally for anything checked-in.
 */

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const { execFileSync } = require('child_process');

const REPO = path.resolve(__dirname, '../../..');
const BOARDS_YAML = path.join(REPO, 'boards/boards.yaml');
const CHECK_SCRIPT = path.join(REPO, 'boards/codegen/check_pin_conflicts.js');
const CHECKED_IN_HEADER = path.join(REPO, 'boards/codegen/hyp_board_config.h');

const testRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-pin-check-'));
process.env.HYPRACCEL_PROJECTS_ROOT = path.join(testRoot, 'projects');
process.env.HYPRACCEL_HARDWARE_CONFIG = path.join(testRoot, 'legacy-hardware.json');
fs.mkdirSync(process.env.HYPRACCEL_PROJECTS_ROOT, { recursive: true });

const { app } = require('../server');

let passed = 0;
let failed = 0;

function test(name, fn) {
    return Promise.resolve()
        .then(fn)
        .then(() => { passed++; console.log(`ok - ${name}`); })
        .catch(err => {
            failed++;
            console.error(`not ok - ${name}`);
            console.error(err && err.stack ? err.stack : err);
        });
}

function i2cSdaOnlyHardware() {
    return {
        version: 1, board: 'esp32', resources: {}, devices: [],
        assignments: [{ node: '', role: 'sda', pin: 'GPIO21', resource: 'i2c.i2c0' }]
    };
}

function uartTxOnlyHardware() {
    return {
        version: 1, board: 'esp32', resources: {}, devices: [],
        assignments: [{ node: '', role: 'tx', pin: 'GPIO1', resource: 'uart.uart0' }]
    };
}

function runCli(payload, args = [BOARDS_YAML]) {
    try {
        const stdout = execFileSync(process.execPath, [CHECK_SCRIPT, ...args], {
            input: typeof payload === 'string' ? payload : JSON.stringify(payload),
            encoding: 'utf8'
        });
        return { status: 0, body: JSON.parse(stdout) };
    } catch (err) {
        // execFileSync throws on non-zero exit; stdout is still captured.
        return { status: err.status, body: JSON.parse(err.stdout) };
    }
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

function snapshotDir(dir) {
    const snapshot = {};
    for (const entry of walk(dir)) {
        const stat = fs.statSync(entry);
        snapshot[entry] = { mtimeMs: stat.mtimeMs, content: stat.isFile() ? fs.readFileSync(entry, 'utf8') : null };
    }
    return snapshot;
}

function* walk(dir) {
    for (const name of fs.readdirSync(dir)) {
        const full = path.join(dir, name);
        yield full;
        if (fs.statSync(full).isDirectory()) yield* walk(full);
    }
}

async function run() {
    const checkedInBackup = fs.readFileSync(CHECKED_IN_HEADER);

    // ---- (1) CLI: I2C SDA-only -> BUS_INCOMPLETE error --------------------
    await test('CLI: I2C SDA-only reports BUS_INCOMPLETE error', () => {
        const res = runCli({ board: 'esp32', hardware: i2cSdaOnlyHardware() });
        assert.equal(res.status, 0);
        assert.deepEqual(res.body.warnings, []);
        assert.equal(res.body.errors.length, 1);
        assert.equal(res.body.errors[0].code, 'BUS_INCOMPLETE');
    });

    // ---- (2) CLI: UART TX-only -> BUS_PARTIAL warning, no errors ----------
    await test('CLI: UART TX-only reports BUS_PARTIAL warning with zero errors', () => {
        const res = runCli({ board: 'esp32', hardware: uartTxOnlyHardware() });
        assert.equal(res.status, 0);
        assert.deepEqual(res.body.errors, []);
        assert.equal(res.body.warnings.length, 1);
        assert.equal(res.body.warnings[0].code, 'BUS_PARTIAL');
    });

    // ---- (3) CLI: malformed input -> exit 2 --------------------------------
    await test('CLI: malformed stdin exits 2 with an error payload', () => {
        const res = runCli('{not valid json');
        assert.equal(res.status, 2);
        assert.equal(typeof res.body.error, 'string');
    });

    // ---- (4) CLI: unknown board -> exit 2 ----------------------------------
    await test('CLI: unknown board exits 2', () => {
        const res = runCli({ board: 'nope', hardware: { board: 'nope', assignments: [] } });
        assert.equal(res.status, 2);
        assert.match(res.body.error, /Unknown board/);
    });

    // ---- server setup -------------------------------------------------------
    const server = await new Promise((resolve, reject) => {
        const instance = app.listen(0, '127.0.0.1', () => resolve(instance));
        instance.once('error', reject);
    });

    try {
        // Write a small project dir to prove /api/hardware/check writes nothing.
        const projectId = 'pin_check_project';
        const projectDir = path.join(process.env.HYPRACCEL_PROJECTS_ROOT, projectId);
        fs.mkdirSync(path.join(projectDir, 'hardware'), { recursive: true });
        fs.mkdirSync(path.join(projectDir, 'graphs'), { recursive: true });
        const now = new Date().toISOString();
        fs.writeFileSync(path.join(projectDir, 'project.json'), JSON.stringify({
            format: 'hypraccel.project', version: 1, id: projectId, name: projectId,
            hardware: { path: 'hardware/hardware.json' }, graphs: { path: 'graphs' },
            createdAt: now, updatedAt: now
        }, null, 2) + '\n', 'utf8');
        fs.writeFileSync(path.join(projectDir, 'hardware', 'hardware.json'),
            JSON.stringify(i2cSdaOnlyHardware(), null, 2) + '\n', 'utf8');

        await new Promise(resolve => setTimeout(resolve, 5)); // let mtimes settle before snapshot

        // ---- (5) POST /api/hardware/check: I2C SDA-only error case --------
        await test('POST /api/hardware/check: I2C SDA-only -> BUS_INCOMPLETE error, project untouched', async () => {
            const before = snapshotDir(projectDir);
            const headerBefore = fs.readFileSync(CHECKED_IN_HEADER, 'utf8');
            const res = await request(server, 'POST', `/api/hardware/check?projectId=${projectId}`, {
                board: 'esp32', assignments: i2cSdaOnlyHardware().assignments
            });
            assert.equal(res.status, 200, JSON.stringify(res.body));
            assert.deepEqual(res.body.warnings, []);
            assert.equal(res.body.errors.length, 1);
            assert.equal(res.body.errors[0].code, 'BUS_INCOMPLETE');
            assert.deepEqual(snapshotDir(projectDir), before, 'project directory must be byte-for-byte unchanged');
            assert.equal(fs.readFileSync(CHECKED_IN_HEADER, 'utf8'), headerBefore, 'checked-in header must be unchanged');
        });

        // ---- (6) POST /api/hardware/check: UART TX-only warning case ------
        await test('POST /api/hardware/check: UART TX-only -> BUS_PARTIAL warning, no errors, nothing written', async () => {
            const before = snapshotDir(projectDir);
            const headerBefore = fs.readFileSync(CHECKED_IN_HEADER, 'utf8');
            const res = await request(server, 'POST', '/api/hardware/check', {
                board: 'esp32', assignments: uartTxOnlyHardware().assignments
            });
            assert.equal(res.status, 200, JSON.stringify(res.body));
            assert.deepEqual(res.body.errors, []);
            assert.equal(res.body.warnings.length, 1);
            assert.equal(res.body.warnings[0].code, 'BUS_PARTIAL');
            assert.deepEqual(snapshotDir(projectDir), before, 'project directory must be byte-for-byte unchanged (no-project-id request)');
            assert.equal(fs.readFileSync(CHECKED_IN_HEADER, 'utf8'), headerBefore, 'checked-in header must be unchanged');
        });

        // ---- (7) POST /api/hardware/check: unknown board -> 400 -----------
        await test('POST /api/hardware/check: unknown board -> 400 with readable message', async () => {
            const res = await request(server, 'POST', '/api/hardware/check', { board: 'nope', assignments: [] });
            assert.equal(res.status, 400);
            assert.match(res.body.error, /Unknown board/);
        });

        // ---- (8) POST /api/hardware/check: missing body -> 400 ------------
        await test('POST /api/hardware/check: missing board/assignments -> 400', async () => {
            const res = await request(server, 'POST', '/api/hardware/check', {});
            assert.equal(res.status, 400);
        });
    } finally {
        server.close();
        fs.writeFileSync(CHECKED_IN_HEADER, checkedInBackup);
    }

    console.log(`check_pin_conflicts.test.js: ${passed} passed, ${failed} failed`);
    if (failed > 0) process.exitCode = 1;
}

run()
    .catch(err => { console.error(err); process.exitCode = 1; })
    .finally(() => { fs.rmSync(testRoot, { recursive: true, force: true }); });
