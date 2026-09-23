'use strict';

/*
 * hardware_check_panel.test.js — tests the SHARED pin-check module,
 * mbd/editor/src/pin_check_panel.js, used by both mbd/editor/src/pin_config.html
 * (the real Hardware Setup screen) and mbd/editor/src/workspace.html (the
 * hardware.json viewer in the project shell). It is a plain classic-script
 * module (window global + module.exports, same pattern as src/board_detail.js)
 * so it can be required directly here — no vm/DOM stubbing needed for the
 * pure formatResult()/renderPanel() functions or the createController()
 * request/debounce/latest-wins logic (fetch, setTimeout and clearTimeout are
 * all injectable).
 *
 * Covers: errors present -> blocking message + items; warnings only -> no
 * blocking message; empty -> "no issues"; loading; request failure -> inline
 * error message; controller latest-request-wins under out-of-order resolves.
 */

const assert = require('assert');
const path = require('path');

const MODULE_PATH = path.join(__dirname, '..', 'src', 'pin_check_panel.js');
const { formatResult, renderPanel, createController } = require(MODULE_PATH);

/* --- minimal stub DOM: only what renderPanel touches -------------------- */
function makeStubDoc() {
    function makeElement(tag) {
        const el = {
            tagName: tag,
            className: '',
            children: [],
            attributes: {},
            _text: '',
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

function collectIssueLines(el) {
    let out = [];
    for (const child of el.children || []) {
        if (child.attributes && child.attributes['data-code']) out.push(child);
        out = out.concat(collectIssueLines(child));
    }
    return out;
}

function findByClass(el, className) {
    for (const child of el.children || []) {
        if (child.className === className) return child;
        const found = findByClass(child, className);
        if (found) return found;
    }
    return null;
}

let passed = 0;
let failed = 0;
function test(name, fn) {
    try {
        fn();
        passed++;
        console.log(`ok - ${name}`);
    } catch (err) {
        failed++;
        console.error(`not ok - ${name}`);
        console.error(err && err.stack ? err.stack : err);
    }
}

/* ---- formatResult (pure) ------------------------------------------------ */
test('formatResult: errors present -> blocking state', () => {
    const summary = formatResult({
        errors: [{ code: 'BUS_INCOMPLETE', message: 'I2C0 is missing SCL.', resource: 'i2c.i2c0' }],
        warnings: []
    });
    assert.equal(summary.state, 'errors');
    assert.equal(summary.blocking, true);
    assert.equal(summary.errorCount, 1);
    assert.equal(summary.warningCount, 0);
});

test('formatResult: warnings only -> non-blocking state', () => {
    const summary = formatResult({
        errors: [],
        warnings: [{ code: 'BUS_PARTIAL', message: 'UART0 TX assigned without RX.', resource: 'uart.uart0' }]
    });
    assert.equal(summary.state, 'warnings-only');
    assert.equal(summary.blocking, false);
    assert.equal(summary.errorCount, 0);
    assert.equal(summary.warningCount, 1);
});

test('formatResult: empty -> empty state', () => {
    const summary = formatResult({ errors: [], warnings: [] });
    assert.equal(summary.state, 'empty');
    assert.equal(summary.blocking, false);
});

test('formatResult: tolerates null/undefined result', () => {
    assert.equal(formatResult(null).state, 'empty');
    assert.equal(formatResult(undefined).state, 'empty');
});

/* ---- renderPanel (DOM writer, stub DOM) --------------------------------- */
test('renderPanel: errors present renders blocking message + error item', () => {
    const doc = makeStubDoc();
    const container = doc.createElement('div');
    const summary = renderPanel(doc, container, {
        status: 'ok',
        result: { errors: [{ code: 'BUS_INCOMPLETE', message: 'I2C0 is missing SCL.', resource: 'i2c.i2c0' }], warnings: [] }
    });
    const blocking = findByClass(container, 'pin-check-blocking');
    assert.ok(blocking, 'blocking message must be rendered');
    assert.match(blocking.textContent, /build will be blocked/i);
    const issues = collectIssueLines(container);
    assert.equal(issues.length, 1);
    assert.equal(issues[0].attributes['data-code'], 'BUS_INCOMPLETE');
    assert.match(issues[0].textContent, /I2C0 is missing SCL/);
    assert.equal(summary.blocking, true);
});

test('renderPanel: warnings-only renders no blocking message but shows the warning', () => {
    const doc = makeStubDoc();
    const container = doc.createElement('div');
    renderPanel(doc, container, {
        status: 'ok',
        result: { errors: [], warnings: [{ code: 'BUS_PARTIAL', message: 'UART0 TX assigned without RX.', resource: 'uart.uart0' }] }
    });
    assert.equal(findByClass(container, 'pin-check-blocking'), null, 'warnings-only must not show a blocking message');
    const issues = collectIssueLines(container);
    assert.equal(issues.length, 1);
    assert.equal(issues[0].attributes['data-code'], 'BUS_PARTIAL');
});

test('renderPanel: empty result shows "no conflicts"', () => {
    const doc = makeStubDoc();
    const container = doc.createElement('div');
    renderPanel(doc, container, { status: 'ok', result: { errors: [], warnings: [] } });
    const empty = findByClass(container, 'pin-check-empty');
    assert.ok(empty, 'empty state element must be rendered');
    assert.match(empty.textContent, /no pin conflicts/i);
});

test('renderPanel: request failure shows an inline "unavailable" message', () => {
    const doc = makeStubDoc();
    const container = doc.createElement('div');
    renderPanel(doc, container, { status: 'request-error', message: 'Failed to fetch' });
    const errEl = findByClass(container, 'pin-check-request-error');
    assert.ok(errEl, 'request-error element must be rendered');
    assert.match(errEl.textContent, /Conflict check unavailable: Failed to fetch/);
});

test('renderPanel: loading state shows a loading message', () => {
    const doc = makeStubDoc();
    const container = doc.createElement('div');
    renderPanel(doc, container, { status: 'loading' });
    const loading = findByClass(container, 'pin-check-loading');
    assert.ok(loading, 'loading element must be rendered');
});

/* ---- createController (request/debounce/latest-wins, injected fetch) --- */

/** A fake timer queue: setTimeoutImpl records callbacks, run() invokes the
 * most-recently-scheduled one (mirrors a real debounce timer). */
function makeFakeTimers() {
    let nextId = 1;
    const scheduled = new Map();
    return {
        setTimeoutImpl(fn) { const id = nextId++; scheduled.set(id, fn); return id; },
        clearTimeoutImpl(id) { scheduled.delete(id); },
        runLatest() {
            const ids = [...scheduled.keys()];
            const id = ids[ids.length - 1];
            const fn = scheduled.get(id);
            scheduled.delete(id);
            fn();
        }
    };
}

test('createController: debounced request reaches fetch with the given payload', async () => {
    const timers = makeFakeTimers();
    let fetchedBody = null;
    const states = [];
    const controller = createController({
        debounceMs: 250,
        setTimeoutImpl: timers.setTimeoutImpl,
        clearTimeoutImpl: timers.clearTimeoutImpl,
        fetchImpl: (url, opts) => {
            fetchedBody = JSON.parse(opts.body);
            return Promise.resolve({ ok: true, json: () => Promise.resolve({ errors: [], warnings: [] }) });
        },
        onState: (s) => states.push(s)
    });
    controller.requestCheck({ board: 'esp32', assignments: [] });
    assert.equal(fetchedBody, null, 'fetch must not fire before the debounce timer runs');
    timers.runLatest();
    await new Promise((resolve) => setImmediate(resolve));
    await new Promise((resolve) => setImmediate(resolve));
    assert.deepEqual(fetchedBody, { board: 'esp32', assignments: [] });
    assert.equal(states[0].status, 'loading');
    assert.equal(states[states.length - 1].status, 'ok');
});

test('createController: latest-request-wins drops a stale in-flight response', async () => {
    const timers = makeFakeTimers();
    const states = [];
    const resolvers = [];
    let call = 0;
    const controller = createController({
        debounceMs: 250,
        setTimeoutImpl: timers.setTimeoutImpl,
        clearTimeoutImpl: timers.clearTimeoutImpl,
        fetchImpl: () => new Promise((resolve) => { resolvers.push(resolve); call++; }),
        onState: (s) => states.push(s)
    });

    // First request starts, then a second (newer) request supersedes it
    // before the first's fetch resolves.
    controller.requestCheck({ board: 'esp32', assignments: [{ pin: 'GPIO1' }] });
    timers.runLatest();
    controller.requestCheck({ board: 'esp32', assignments: [{ pin: 'GPIO2' }] });
    timers.runLatest();
    assert.equal(call, 2, 'both requests must have started a fetch');

    // Resolve the STALE (first) request last, and the newer one first.
    resolvers[1]({ ok: true, json: () => Promise.resolve({ errors: [], warnings: [{ code: 'BUS_PARTIAL', message: 'x' }] }) });
    await new Promise((resolve) => setImmediate(resolve));
    await new Promise((resolve) => setImmediate(resolve));
    resolvers[0]({ ok: true, json: () => Promise.resolve({ errors: [{ code: 'BUS_INCOMPLETE', message: 'stale' }], warnings: [] }) });
    await new Promise((resolve) => setImmediate(resolve));
    await new Promise((resolve) => setImmediate(resolve));

    const okStates = states.filter((s) => s.status === 'ok');
    assert.equal(okStates.length, 1, 'the stale response must never reach onState as "ok"');
    assert.equal(okStates[0].result.warnings[0].code, 'BUS_PARTIAL');
});

test('createController: HTTP error response surfaces as request-error', async () => {
    const timers = makeFakeTimers();
    const states = [];
    const controller = createController({
        debounceMs: 250,
        setTimeoutImpl: timers.setTimeoutImpl,
        clearTimeoutImpl: timers.clearTimeoutImpl,
        fetchImpl: () => Promise.resolve({ ok: false, json: () => Promise.resolve({ error: "Unknown board 'nope'." }) }),
        onState: (s) => states.push(s)
    });
    controller.requestCheckNow({ board: 'nope', assignments: [] });
    await new Promise((resolve) => setImmediate(resolve));
    await new Promise((resolve) => setImmediate(resolve));
    const last = states[states.length - 1];
    assert.equal(last.status, 'request-error');
    assert.match(last.message, /Unknown board/);
});

console.log(`hardware_check_panel.test.js: ${passed} passed, ${failed} failed`);
if (failed > 0) process.exitCode = 1;
