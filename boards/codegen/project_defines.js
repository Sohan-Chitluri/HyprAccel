'use strict';
/*
 * project_defines.js — the ONE place that turns a merged project config into
 * hyp_board_config.h project macros. CONTRACT (owned by the integrator).
 * Replaces server.js injectHardwareHeader() and the inline copy in POST /api/generate.
 *
 * Two variants reproduce two historical code paths byte-for-byte when the
 * config carries no desktop data (clock === null and every assignment has a
 * non-empty node):
 *   - 'materialize': mbd/editor/server.js injectHardwareHeader()
 *   - 'api':         mbd/editor/server.js POST /api/generate inline block
 * When the config DOES carry desktop data (clock !== null, or at least one
 * assignment has an empty node, i.e. a desktop-only pin with no graph node),
 * additional blocks are appended: pin-function macros, clock macros, ESP32
 * clock aliases, and configuration warnings. See individual builders below.
 */

const MARKER = '#endif /* HYP_BOARD_CONFIG_H */';

/* -------------------------------------------------------------------------
 * Shared macro-name helpers (ported verbatim from server.js)
 * ---------------------------------------------------------------------- */

function assignmentMacroBase(assignment) {
    return assignment.node.replace(/[^A-Za-z0-9_]/g, '_').replace(/_+/g, '_').replace(/_$/, '').toUpperCase();
}

function peripheralMacroBase(assignment) {
    const resource = assignment.resource.replace(/[^A-Za-z0-9_]/g, '_').replace(/_+/g, '_').replace(/_$/, '').toUpperCase();
    return `${assignmentMacroBase(assignment)}_${resource}`;
}

/** Uppercase, [A-Z0-9_] only — used for macro name fragments derived from
 * arbitrary strings (pin functions, clock mux selections, etc). */
function sanitizeMacro(value) {
    return String(value).toUpperCase().replace(/[^A-Z0-9_]/g, '_');
}

/** Comments in the generated header must never contain a literal comment
 * terminator (it would close the C comment early). Also collapse newlines
 * so a multi-line warning/error string can't break out of a single-line
 * comment. */
function escapeComment(value) {
    return String(value).replace(/\r?\n/g, ' ').split('*/').join('* /');
}

/** Integers render without decimals; otherwise up to 6 significant digits,
 * with trailing zeros trimmed. */
function formatClockNumber(value) {
    const num = Number(value);
    if (Number.isInteger(num)) return String(num);
    let s = num.toPrecision(6);
    if (s.indexOf('e') === -1 && s.indexOf('E') === -1) {
        s = s.replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '');
    }
    return s;
}

/* -------------------------------------------------------------------------
 * Legacy blocks — MBD pin assignments + Hardware Setup resources.
 * Ported from injectHardwareHeader (variant 'materialize') and the inline
 * code in POST /api/generate (variant 'api'). The only behavioural change
 * from the originals: assignments with an empty `node` (desktop-only pins,
 * see REQUIREMENTS #2) never produce HYP_PIN_ / HYP_PERIPH_ macros, since
 * those macros are keyed by node and would collide across desktop pins.
 * ---------------------------------------------------------------------- */

function buildPinDefines(assignments, commentText) {
    const pinDefines = ['', commentText];
    const definedPeripherals = new Set();
    for (const a of assignments) {
        if (!a.node) continue;
        const macroBase = assignmentMacroBase(a);
        pinDefines.push(`#define HYP_PIN_${macroBase}_${a.role.toUpperCase()} "${a.pin}"  /* ${a.node} → ${a.resource}.${a.role} */`);
        const peripheralMacro = peripheralMacroBase(a);
        if (!definedPeripherals.has(peripheralMacro)) {
            pinDefines.push(`#define HYP_PERIPH_${peripheralMacro} "${a.resource}"`);
            definedPeripherals.add(peripheralMacro);
        }
    }
    return pinDefines;
}

function buildResourceDefines(assignments, resources, commentText, includeAccelerator) {
    const resourceDefines = ['', commentText];
    for (const resource of Object.values(resources)) {
        const macro = resource.id.toUpperCase().replace(/[^A-Z0-9_]/g, '_');
        const assigned = assignments.filter(item => item.resource === resource.id);
        if (assigned.length > 0 || (includeAccelerator && resource.type === 'accelerator')) {
            resourceDefines.push(`#define HYP_RESOURCE_${macro} 1`);
        }
        for (const [key, value] of Object.entries(resource.configuration || {})) {
            const keyMacro = key.replace(/([a-z])([A-Z])/g, '$1_$2').toUpperCase().replace(/[^A-Z0-9_]/g, '_');
            resourceDefines.push(`#define HYP_RESOURCE_${macro}_${keyMacro} ${typeof value === 'number' ? value : JSON.stringify(String(value))}`);
        }
    }
    return resourceDefines;
}

function buildLegacyBlocks(config, variant) {
    const assignments = config.assignments || [];
    const resources = config.resources || {};

    if (variant === 'materialize') {
        const pinDefines = buildPinDefines(assignments, '/* MBD Pin Assignments — generated from project hardware.json */');
        const resourceDefines = buildResourceDefines(assignments, resources, '/* Hardware Setup resources — generated from project hardware.json */', true);
        return pinDefines.concat(resourceDefines).join('\n');
    }

    /* variant === 'api' */
    let text = '';
    if (assignments.length > 0) {
        const pinDefines = buildPinDefines(assignments, '/* MBD Pin Assignments — generated by pin_config UI (MBD-T1b) */');
        text += pinDefines.join('\n') + '\n\n';
    }
    const resourceDefines = buildResourceDefines(assignments, resources, '/* Hardware Setup resources — generated from project hardware.json */', false);
    text += resourceDefines.join('\n');
    return text;
}

/* -------------------------------------------------------------------------
 * New blocks — only emitted when the config carries desktop data.
 * ---------------------------------------------------------------------- */

/** Derives the pin "function" string for the pin-function block. */
function pinFunctionFor(a) {
    const type = String(a.resource || '').split('.')[0];
    if (type === 'gpio' && a.role === 'gpio') return 'gpio';
    if (type === 'pwm' && a.role === 'output') return 'pwm';
    if (type === 'adc' && a.role === 'input') return 'adc';
    return `${a.resource}.${a.role}`;
}

function buildPinFunctionBlock(assignments) {
    const lines = ['/* Pin functions — generated from project hardware.json (pin → function) */'];
    const byPin = new Map();
    for (const a of assignments) {
        if (!a.pin) continue;
        byPin.set(a.pin, a);
    }
    const pins = Array.from(byPin.keys()).sort();
    for (const pin of pins) {
        const a = byPin.get(pin);
        const func = pinFunctionFor(a);
        lines.push(`#define HYP_PINFUNC_${sanitizeMacro(pin)} "${func}"`);
        lines.push(`#define HYP_PINFUNC_${sanitizeMacro(pin)}_${sanitizeMacro(func)} 1`);
    }
    return lines.join('\n');
}

/* ESP32-only clock alias table: which boards.yaml clock node feeds which
 * legacy HYP_* alias macro, and how mux selections map onto the alias's
 * suffix. Kept small and local — this is the only board-specific behaviour
 * in this module (REQUIREMENTS #3c). */
const ESP32_CLOCK_ALIASES = {
    board: 'esp32',
    cpuFreqNode: 'cpu_div',
    apbFreqNode: 'apb_clk',
    cpuMuxNode: 'cpu_mux',
    cpuMuxMap: { bbpll: 'PLL', xtal: 'XTAL', rc_fast: 'RC_FAST' },
    uartClkSelNode: 'uart_clk_sel',
    uartClkSelMap: { apb_clk: 'APB', ref_tick: 'REF_TICK' },
    ledcClkSelNode: 'ledc_clk_sel',
    ledcClkSelMap: { apb_clk: 'APB', rc_fast: 'RC_FAST' },
};
const ESP32_STANDARD_CPU_FREQS_MHZ = [80, 160, 240];

function findClockNode(nodes, id) {
    return nodes.find(n => n.id === id) || null;
}

/** Effective value for a clock node: a hardware-coupled node (`follows` in
 * boards.yaml) takes its value from the followed node's effective value via
 * its map, mirroring computeClocks() in desktop/src/clock/clock_tree.cpp;
 * otherwise the explicit selection if present, else the boards.yaml default. */
function effectiveSelection(selections, nodes, nodeId) {
    const node = findClockNode(nodes, nodeId);
    if (node && node.follows && node.follows.node) {
        const source = effectiveSelection(selections, nodes, node.follows.node);
        for (const [key, value] of Object.entries(node.follows.map || {})) {
            if (String(key) === String(source) || (Number.isFinite(Number(key)) && Number(key) === Number(source))) {
                return String(value);
            }
        }
        return node.default;
    }
    if (Object.prototype.hasOwnProperty.call(selections, nodeId)) return selections[nodeId];
    return node ? node.default : undefined;
}

function buildEsp32ClockAliases(config, nodes) {
    const lines = [];
    const clock = config.clock;
    const resolved = clock.resolved || {};
    const selections = clock.selections || {};
    const t = ESP32_CLOCK_ALIASES;

    if (Object.prototype.hasOwnProperty.call(resolved, t.cpuFreqNode)) {
        const cpuFreq = resolved[t.cpuFreqNode];
        lines.push(`#define HYP_CPU_FREQ_MHZ ${formatClockNumber(cpuFreq)}`);
        if (!ESP32_STANDARD_CPU_FREQS_MHZ.includes(Number(cpuFreq))) {
            lines.push('#define HYP_CLOCK_NONSTANDARD_CPU_FREQ 1');
            lines.push('/* HYP_CPU_FREQ_MHZ is not one of the ESP-IDF supported CPU frequencies (80/160/240 MHz) */');
        }
    }
    if (Object.prototype.hasOwnProperty.call(resolved, t.apbFreqNode)) {
        lines.push(`#define HYP_APB_FREQ_MHZ ${formatClockNumber(resolved[t.apbFreqNode])}`);
    }

    const cpuMuxSel = effectiveSelection(selections, nodes, t.cpuMuxNode);
    if (cpuMuxSel != null && t.cpuMuxMap[cpuMuxSel]) {
        lines.push(`#define HYP_CPU_CLK_SRC_${t.cpuMuxMap[cpuMuxSel]} 1`);
    }
    const uartSel = effectiveSelection(selections, nodes, t.uartClkSelNode);
    if (uartSel != null && t.uartClkSelMap[uartSel]) {
        lines.push(`#define HYP_UART_CLK_SRC_${t.uartClkSelMap[uartSel]} 1`);
    }
    const ledcSel = effectiveSelection(selections, nodes, t.ledcClkSelNode);
    if (ledcSel != null && t.ledcClkSelMap[ledcSel]) {
        lines.push(`#define HYP_LEDC_CLK_SRC_${t.ledcClkSelMap[ledcSel]} 1`);
    }
    return lines;
}

function buildClockBlock(config, boardData) {
    const clock = config.clock;
    const nodes = (boardData && boardData.clocks && boardData.clocks.nodes) || [];
    const resolved = clock.resolved || {};
    const selections = clock.selections || {};
    const unverified = new Set(clock.unverifiedNodes || []);

    const lines = [
        '/*',
        ' * Clock configuration — best-effort values computed by HyprAccel Studio',
        ' * at project save time. These values are NOT verified against the',
        " * board's datasheet.",
        ' */',
        '#define HYP_CLOCK_CONFIG_PRESENT 1',
        '#define HYP_CLOCK_BEST_EFFORT 1',
    ];

    for (const node of nodes) {
        const id = node.id;
        if (Object.prototype.hasOwnProperty.call(resolved, id)) {
            const suffix = unverified.has(id) ? '  /* unverified */' : '';
            lines.push(`#define HYP_CLOCK_${sanitizeMacro(id)}_MHZ ${formatClockNumber(resolved[id])}${suffix}`);
        }
        if (node.kind === 'mux') {
            const effective = effectiveSelection(selections, nodes, id);
            if (effective != null) {
                lines.push(`#define HYP_CLOCK_${sanitizeMacro(id)}_SRC_${sanitizeMacro(effective)} 1`);
            }
        } else if (node.kind === 'pll' || node.kind === 'divider') {
            const effective = effectiveSelection(selections, nodes, id);
            if (effective != null) {
                lines.push(`#define HYP_CLOCK_${sanitizeMacro(id)}_FACTOR ${Number(effective)}`);
            }
        }
    }

    if (config.board === ESP32_CLOCK_ALIASES.board) {
        lines.push(...buildEsp32ClockAliases(config, nodes));
    }

    if (Array.isArray(clock.errors) && clock.errors.length > 0) {
        lines.push('#define HYP_CLOCK_HAS_ERRORS 1');
        for (const err of clock.errors) lines.push(`/* ${escapeComment(err)} */`);
    }

    return lines.join('\n');
}

function buildWarningsBlock(warnings) {
    const lines = ['/* Configuration warnings: */'];
    for (const w of warnings) lines.push(`/* ${escapeComment(w)} */`);
    return lines.join('\n');
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @param config    merged config from project_config.js (clock may be null)
 * @param boardData parsed boards.yaml boards[config.board]
 * @param options   { variant: 'materialize' | 'api', warnings: [string] }
 * @returns the text inserted immediately before '#endif /* HYP_BOARD_CONFIG_H *\/'
 */
function renderProjectDefines(config, boardData, options) {
    options = options || {};
    const variant = options.variant === 'api' ? 'api' : 'materialize';
    const assignments = config.assignments || [];

    const blocks = [buildLegacyBlocks(config, variant)];

    const desktopTriggered = config.clock != null || assignments.some(a => a.node === '');
    if (desktopTriggered) {
        blocks.push(buildPinFunctionBlock(assignments));
        if (config.clock != null) {
            blocks.push(buildClockBlock(config, boardData));
        }
    }

    const warnings = []
        .concat(Array.isArray(options.warnings) ? options.warnings : [])
        .concat(Array.isArray(config.configWarnings) ? config.configWarnings : []);
    if (warnings.length > 0) {
        blocks.push(buildWarningsBlock(warnings));
    }

    return blocks.join('\n\n');
}

/** headerText with renderProjectDefines(...) inserted before the #endif marker. */
function injectProjectDefines(headerText, config, boardData, options) {
    const insertion = renderProjectDefines(config, boardData, options);
    return headerText.replace(MARKER, insertion + '\n\n' + MARKER);
}

module.exports = { renderProjectDefines, injectProjectDefines };
