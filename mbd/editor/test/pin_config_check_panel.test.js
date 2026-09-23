'use strict';

/*
 * pin_config_check_panel.test.js — headless coverage for the live
 * pin-conflict check as wired into the REAL Hardware Setup screen,
 * mbd/editor/src/pin_config.html.
 *
 * There is no browser in this environment, and pin_config.html's own change
 * path (drag/drop onto .slot-drop elements, board fetch/render, the whole
 * `nodes`/`assignments` state machine) is heavy enough — real
 * dragstart/drop DOM events, canvas-free layout, `fetch('/api/boards')`
 * bootstrapping — that vm-executing the whole inline script headlessly would
 * mean reimplementing a good chunk of a DOM/drag-drop shim, which would test
 * the shim more than the page. Instead this file:
 *
 *   (1) Statically confirms pin_config.html actually includes
 *       <script src="pin_check_panel.js"> before its own inline script, and
 *       that it no longer defines its own copy of the panel logic (i.e. it
 *       reuses the shared module rather than duplicating it) — the same
 *       static check is done for workspace.html.
 *   (2) Drives the EXACT code pin_config.html's requestPinCheck() calls —
 *       window.HyprPinCheckPanel.createController() + .renderPanel() from
 *       the real src/pin_check_panel.js module — against a REAL running
 *       instance of the editor server (mbd/editor/server.js) on a temp
 *       HYPRACCEL_PROJECTS_ROOT, exactly as new_project_flow.test.js starts
 *       its server. This exercises the real POST /api/hardware/check
 *       request/response cycle and the real rendering function pin_config's
 *       requestPinCheck()/pinCheckController wire together, with only the
 *       DOM (not the network, not the server, not the panel logic) stubbed.
 *
 * See check_pin_conflicts.test.js for the CLI + POST /api/hardware/check
 * cases in isolation, and hardware_check_panel.test.js for the shared
 * module's pure-function/controller unit tests (including latest-wins).
 */

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const REPO = path.resolve(__dirname, '../../..');
const PIN_CONFIG_HTML = path.join(REPO, 'mbd/editor/src/pin_config.html');
const WORKSPACE_HTML = path.join(REPO, 'mbd/editor/src/workspace.html');

const testRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-pin-config-check-'));
process.env.HYPRACCEL_PROJECTS_ROOT = path.join(testRoot, 'projects');
process.env.HYPRACCEL_HARDWARE_CONFIG = path.join(testRoot, 'legacy-hardware.json');
fs.mkdirSync(process.env.HYPRACCEL_PROJECTS_ROOT, { recursive: true });

const { app } = require('../server');
const { createController, renderPanel } = require('../src/pin_check_panel.js');

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

/* --- minimal stub DOM: only what renderPanel touches -------------------- */
function makeStubDoc() {
    function makeElement(tag) {
        const el = {
            tagName: tag, className: '', children: [], attributes: {}, _text: '',
            get textContent() { return this._text; },
            set textContent(v) { this._text = String(v); this.children = []; },
            get innerHTML() { return this._html || ''; },
            set innerHTML(v) { this._html = v; if (v === '') this.children = []; },
            appendChild(child) { this.children.push(child); return child; },
            setAttribute(name, value) { this.attributes[name] = value; }
        };
        return el;
    }
    return { createElement: makeElement };
}

function findByClass(el, className) {
    for (const child of el.children || []) {
        if (child.className === className) return child;
        const found = findByClass(child, className);
        if (found) return found;
    }
    return null;
}

/* ---- (1) static: both pages load the shared module, not a private copy - */
test('pin_config.html includes <script src="pin_check_panel.js"> before its inline script', () => {
    const html = fs.readFileSync(PIN_CONFIG_HTML, 'utf8');
    const scriptTagIdx = html.indexOf('<script src="pin_check_panel.js">');
    assert.ok(scriptTagIdx >= 0, 'pin_config.html must include the shared module');
    const inlineIdx = html.indexOf('<script>\n\'use strict\';');
    assert.ok(inlineIdx >= 0, 'pin_config.html must still have its own inline <script>');
    assert.ok(scriptTagIdx < inlineIdx, 'the shared module must load before the inline script uses it');
    assert.ok(!/function\s+formatPinCheckResult/.test(html), 'pin_config.html must not define its own copy of formatPinCheckResult');
    assert.ok(!/function\s+renderPinCheckPanel/.test(html), 'pin_config.html must not define its own copy of renderPinCheckPanel');
    assert.ok(html.includes('window.HyprPinCheckPanel.createController'), 'pin_config.html must call the shared controller factory');
});

test('workspace.html includes <script src="pin_check_panel.js"> and reuses the shared module (no private copy)', () => {
    const html = fs.readFileSync(WORKSPACE_HTML, 'utf8');
    assert.ok(html.includes('<script src="pin_check_panel.js"></script>'), 'workspace.html must include the shared module');
    assert.ok(!/function\s+formatPinCheckResult/.test(html), 'workspace.html must not define its own copy of formatPinCheckResult');
    assert.ok(!/function\s+renderPinCheckPanel/.test(html), 'workspace.html must not define its own copy of renderPinCheckPanel');
    assert.ok(html.includes('window.HyprPinCheckPanel.createController'), 'workspace.html must call the shared controller factory');
});

/* ---- (2) live-server integration through the exact shared code path ---- */
async function run() {
    const server = await new Promise((resolve, reject) => {
        const instance = app.listen(0, '127.0.0.1', () => resolve(instance));
        instance.once('error', reject);
    });
    const base = `http://127.0.0.1:${server.address().port}`;

    try {
        await test('pin_config.html-equivalent: I2C SDA-only shows the blocking error panel via a real server round trip', async () => {
            const doc = makeStubDoc();
            const panel = doc.createElement('div');
            let lastSummary;
            const controller = createController({
                debounceMs: 0,
                fetchImpl: (url, opts) => fetch(base + url, opts),
                onState(state) {
                    lastSummary = renderPanel(doc, panel, state);
                }
            });
            controller.requestCheckNow({
                board: 'esp32',
                assignments: [{ node: '', role: 'sda', pin: 'GPIO21', resource: 'i2c.i2c0' }],
                configurations: {}, devices: []
            });
            // requestCheckNow's fetch is async; poll briefly for the terminal render.
            for (let i = 0; i < 50 && !lastSummary; i++) await new Promise(r => setTimeout(r, 10));
            assert.ok(lastSummary, 'renderPanel must have run to completion');
            const blocking = findByClass(panel, 'pin-check-blocking');
            assert.ok(blocking, 'blocking message must be rendered for I2C SDA-only');
            assert.match(blocking.textContent, /build will be blocked/i);
            assert.equal(lastSummary.errorCount, 1);
            assert.equal(lastSummary.errors[0].code, 'BUS_INCOMPLETE');
        });

        await test('pin_config.html-equivalent: UART TX-only shows warning-only (no blocking message) via a real server round trip', async () => {
            const doc = makeStubDoc();
            const panel = doc.createElement('div');
            let lastSummary;
            const controller = createController({
                debounceMs: 0,
                fetchImpl: (url, opts) => fetch(base + url, opts),
                onState(state) {
                    lastSummary = renderPanel(doc, panel, state);
                }
            });
            controller.requestCheckNow({
                board: 'esp32',
                assignments: [{ node: '', role: 'tx', pin: 'GPIO1', resource: 'uart.uart0' }],
                configurations: {}, devices: []
            });
            for (let i = 0; i < 50 && !lastSummary; i++) await new Promise(r => setTimeout(r, 10));
            assert.ok(lastSummary, 'renderPanel must have run to completion');
            assert.equal(findByClass(panel, 'pin-check-blocking'), null, 'warnings-only must not show a blocking message');
            assert.equal(lastSummary.errorCount, 0);
            assert.equal(lastSummary.warningCount, 1);
            assert.equal(lastSummary.warnings[0].code, 'BUS_PARTIAL');
        });
    } finally {
        server.close();
    }

    console.log(`pin_config_check_panel.test.js: ${passed} passed, ${failed} failed`);
    if (failed > 0) process.exitCode = 1;
}

run()
    .catch(err => { console.error(err); process.exitCode = 1; })
    .finally(() => { fs.rmSync(testRoot, { recursive: true, force: true }); });
