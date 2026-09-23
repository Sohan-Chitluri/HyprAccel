// pin_check_panel.js — plain classic browser script, no ES modules, no deps.
// Shared live pin-conflict-check panel logic for the Hardware Setup screen
// (mbd/editor/src/pin_config.html) and the Project Workspace's hardware.json
// viewer (mbd/editor/src/workspace.html). Both pages POST their current
// in-UI hardware state to /api/hardware/check (mbd/editor/server.js), which
// runs the SAME checkPinConflicts() codegen would block on — no conflict
// rules are reimplemented here, only formatting/rendering/request-plumbing.
//
// Exposed as window.HyprPinCheckPanel = { formatResult, renderPanel, createController }
// and, under Node, as module.exports = { ... } (for unit tests — see
// mbd/editor/test/hardware_check_panel.test.js).

(function (root) {
  'use strict';

  /**
   * Pure: turn a { errors, warnings } result (or null/undefined) into a
   * plain summary object the renderer (and tests) can consume without a DOM.
   */
  function formatResult(result) {
    var errors = (result && Array.isArray(result.errors)) ? result.errors : [];
    var warnings = (result && Array.isArray(result.warnings)) ? result.warnings : [];
    var blocking = errors.length > 0;
    var state = blocking ? 'errors' : (warnings.length > 0 ? 'warnings-only' : 'empty');
    return {
      state: state,
      blocking: blocking,
      errorCount: errors.length,
      warningCount: warnings.length,
      errors: errors,
      warnings: warnings
    };
  }

  function issueLine(doc, issue, kind) {
    var line = doc.createElement('div');
    line.className = 'pin-check-issue pin-check-issue-' + kind;
    var location = issue.pin ? ('[' + issue.pin + '] ') : (issue.resource ? ('[' + issue.resource + '] ') : '');
    line.textContent = location + issue.message;
    if (issue.pin) line.setAttribute('data-pin', issue.pin);
    if (issue.resource) line.setAttribute('data-resource', issue.resource);
    if (issue.code) line.setAttribute('data-code', issue.code);
    return line;
  }

  /**
   * Renders `state` into `container` (an element supporting createElement-
   * style children, appendChild and innerHTML = '' for clearing).
   *   state = {
   *     status: 'loading' | 'request-error' | 'ok',
   *     result?: { errors, warnings },   // when status === 'ok'
   *     message?: string                 // when status === 'request-error'
   *   }
   * Returns the summary object formatResult() produced (or null for
   * loading/request-error), so callers can drive row highlighting from the
   * same pass without recomputing it.
   */
  function renderPanel(doc, container, state) {
    container.innerHTML = '';
    state = state || { status: 'ok', result: null };

    if (state.status === 'loading') {
      var loading = doc.createElement('div');
      loading.className = 'pin-check-loading';
      loading.textContent = 'Checking pin assignments…';
      container.appendChild(loading);
      return null;
    }

    if (state.status === 'request-error') {
      var errEl = doc.createElement('div');
      errEl.className = 'pin-check-request-error';
      errEl.textContent = 'Conflict check unavailable: ' + (state.message || 'request failed.');
      container.appendChild(errEl);
      return null;
    }

    var summary = formatResult(state.result);

    if (summary.state === 'empty') {
      var empty = doc.createElement('div');
      empty.className = 'pin-check-empty';
      empty.textContent = 'No pin conflicts.';
      container.appendChild(empty);
      return summary;
    }

    if (summary.blocking) {
      var blockMsg = doc.createElement('div');
      blockMsg.className = 'pin-check-blocking';
      blockMsg.textContent = summary.errorCount + ' pin conflict error' + (summary.errorCount === 1 ? '' : 's')
        + ' — build will be blocked until these are resolved.';
      container.appendChild(blockMsg);
    }

    if (summary.errorCount > 0) {
      var errorsList = doc.createElement('div');
      errorsList.className = 'pin-check-errors';
      summary.errors.forEach(function (issue) { errorsList.appendChild(issueLine(doc, issue, 'error')); });
      container.appendChild(errorsList);
    }

    if (summary.warningCount > 0) {
      var warningsList = doc.createElement('div');
      warningsList.className = 'pin-check-warnings';
      summary.warnings.forEach(function (issue) { warningsList.appendChild(issueLine(doc, issue, 'warning')); });
      container.appendChild(warningsList);
    }

    return summary;
  }

  /**
   * A small stateful controller: debounces (default 250ms) requests to
   * POST /api/hardware/check and guarantees "latest request wins" (an older
   * in-flight response arriving after a newer request was made is dropped).
   * `options`:
   *   fetchImpl   - defaults to the global fetch (injectable for tests)
   *   endpoint    - defaults to '/api/hardware/check'
   *   debounceMs  - defaults to 250
   *   onState(state) - called with the renderPanel()-shaped state object on
   *                     every transition (loading -> ok|request-error)
   *   setTimeoutImpl / clearTimeoutImpl - injectable for tests
   */
  function createController(options) {
    options = options || {};
    var debounceMs = options.debounceMs != null ? options.debounceMs : 250;
    var endpoint = options.endpoint || '/api/hardware/check';
    var setTimeoutImpl = options.setTimeoutImpl || (typeof setTimeout !== 'undefined' ? setTimeout : null);
    var clearTimeoutImpl = options.clearTimeoutImpl || (typeof clearTimeout !== 'undefined' ? clearTimeout : null);
    var fetchImpl = options.fetchImpl || (typeof fetch !== 'undefined' ? fetch : null);
    var onState = options.onState || function () {};

    var timer = null;
    var token = 0;

    function cancel() {
      if (timer != null && clearTimeoutImpl) clearTimeoutImpl(timer);
      timer = null;
    }

    function runNow(payload) {
      var myToken = ++token;
      onState({ status: 'loading' });
      if (!fetchImpl) {
        onState({ status: 'request-error', message: 'fetch is not available.' });
        return;
      }
      fetchImpl(endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      }).then(function (res) {
        return res.json().catch(function () { return {}; }).then(function (body) {
          return { ok: res.ok, body: body };
        });
      }).then(function (outcome) {
        if (myToken !== token) return; // superseded by a newer request
        if (!outcome.ok) {
          onState({ status: 'request-error', message: (outcome.body && outcome.body.error) || 'request failed.' });
          return;
        }
        onState({ status: 'ok', result: outcome.body });
      }).catch(function (err) {
        if (myToken !== token) return;
        onState({ status: 'request-error', message: err && err.message ? err.message : String(err) });
      });
    }

    /** Debounced entry point: call on every in-UI assignment/resource change. */
    function requestCheck(payload) {
      cancel();
      if (!setTimeoutImpl) { runNow(payload); return; }
      timer = setTimeoutImpl(function () { runNow(payload); }, debounceMs);
    }

    /** Bypasses the debounce; used by tests and "run once on open". */
    function requestCheckNow(payload) {
      cancel();
      runNow(payload);
    }

    return { requestCheck: requestCheck, requestCheckNow: requestCheckNow, cancel: cancel };
  }

  var api = { formatResult: formatResult, renderPanel: renderPanel, createController: createController };

  if (typeof window !== 'undefined') {
    window.HyprPinCheckPanel = api;
  }
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = api;
  }
})(typeof window !== 'undefined' ? window : this);
