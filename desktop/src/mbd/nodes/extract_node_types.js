#!/usr/bin/env node
'use strict';

// Regenerates desktop/src/mbd/nodes/node_types.json from the frozen web
// sources (mbd/editor/src/graph_editor.html, mbd/codegen/graph_to_c.js).
// See desktop/docs/mbd_graph_contract.md §6 D1.
//
// Usage:
//   node extract_node_types.js [--check] [--html <path>] [--codegen <path>]
//                               [--out <path>]
//
// --check compares the freshly extracted data against the committed --out
// file and exits 1 (with a readable diff summary) if they differ.
//
// The extractor never guesses. Any expected pattern that fails to match
// exits with code 2 and a message naming the missing pattern, rather than
// emitting partial/incorrect data.

const fs = require('fs');
const path = require('path');

function fail(message) {
    process.stderr.write('extract_node_types: ' + message + '\n');
    process.exit(2);
}

function requireMatch(re, text, what) {
    const m = re.exec(text);
    if (!m) fail(`pattern not found: ${what}`);
    return m;
}

function parseArgs(argv) {
    const repoRoot = path.join(__dirname, '..', '..', '..', '..');
    const opts = {
        check: false,
        html: path.join(repoRoot, 'mbd', 'editor', 'src', 'graph_editor.html'),
        codegen: path.join(repoRoot, 'mbd', 'codegen', 'graph_to_c.js'),
        out: path.join(__dirname, 'node_types.json'),
    };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--check') opts.check = true;
        else if (a === '--html') opts.html = argv[++i];
        else if (a === '--codegen') opts.codegen = argv[++i];
        else if (a === '--out') opts.out = argv[++i];
        else fail(`unknown argument '${a}'`);
    }
    return opts;
}

// ---------------------------------------------------------------------------
// graph_editor.html extraction
// ---------------------------------------------------------------------------

function extractEditorData(html) {
    const litStart = html.indexOf("const defaults = {");
    const litEnd = html.indexOf('function makeNode');
    if (litStart < 0 || litEnd < 0 || litEnd <= litStart) {
        fail('could not locate the `const defaults = {` ... `function makeNode` literal block');
    }
    const literalBlock = html.slice(litStart, litEnd);

    const selStart = html.indexOf('function getSelectOptions');
    const selEnd = html.indexOf('function field(');
    if (selStart < 0 || selEnd < 0 || selEnd <= selStart) {
        fail('could not locate `function getSelectOptions` ... `function field(`');
    }
    const selectFnBlock = html.slice(selStart, selEnd);

    let evaluated;
    try {
        // eslint-disable-next-line no-new-func
        evaluated = new Function(
            literalBlock + selectFnBlock +
            ';return {defaults, descriptions, displayNames, categories, portTypes, ports, getSelectOptions};'
        )();
    } catch (e) {
        fail(`failed to evaluate the extracted literal block: ${e.message}`);
    }
    for (const key of ['defaults', 'descriptions', 'displayNames', 'categories', 'portTypes', 'ports', 'getSelectOptions']) {
        if (evaluated[key] === undefined) fail(`extracted literal block is missing '${key}'`);
    }

    // renderConfig(): the paramGroups if-chain.
    const rcStart = html.indexOf('function renderConfig');
    const rcEnd = html.indexOf('function toGraph');
    if (rcStart < 0 || rcEnd < 0 || rcEnd <= rcStart) {
        fail('could not locate `function renderConfig` ... `function toGraph`');
    }
    const renderConfigBlock = html.slice(rcStart, rcEnd);

    const groupsByType = {};
    const groupChainRe = /if \(t === '(\w+)'((?:\s*\|\|\s*t === '\w+')*)\)\s*\{\s*(?:\/\/[^\n]*\n\s*)?paramGroups = (\[[\s\S]*?\]);\s*\}/g;
    let gm;
    let groupMatches = 0;
    while ((gm = groupChainRe.exec(renderConfigBlock)) !== null) {
        groupMatches++;
        const types = [gm[1]];
        const orChain = gm[2] || '';
        const orRe = /t === '(\w+)'/g;
        let om;
        while ((om = orRe.exec(orChain)) !== null) types.push(om[1]);
        let groupsValue;
        try {
            // eslint-disable-next-line no-new-func
            groupsValue = new Function('return ' + gm[3])();
        } catch (e) {
            fail(`failed to evaluate paramGroups for ${types.join(', ')}: ${e.message}`);
        }
        for (const t of types) groupsByType[t] = groupsValue;
    }
    if (groupMatches === 0) fail('the renderConfig paramGroups if-chain did not match any branch');

    const missingGroups = Object.keys(evaluated.defaults).filter(t => !(t in groupsByType));
    if (missingGroups.length) {
        fail(`renderConfig has no paramGroups branch for: ${missingGroups.join(', ')}`);
    }

    // expectedTypeMap (hardware resource type prefixes per node type).
    const hwMapMatch = requireMatch(/const expectedTypeMap = (\{[\s\S]*?\});/, renderConfigBlock, 'expectedTypeMap');
    let expectedTypeMap;
    try {
        // eslint-disable-next-line no-new-func
        expectedTypeMap = new Function('return ' + hwMapMatch[1])();
    } catch (e) {
        fail(`failed to evaluate expectedTypeMap: ${e.message}`);
    }

    // The hardware-group type list: `if ([...].includes(t)) {` guarding the
    // "Hardware Setup" inspector group.
    const hwGroupMatch = requireMatch(/if \((\[[^\]]*\])\.includes\(t\)\) \{/, renderConfigBlock, 'hardware-group type list');
    let hardwareGroupTypes;
    try {
        // eslint-disable-next-line no-new-func
        hardwareGroupTypes = new Function('return ' + hwGroupMatch[1])();
    } catch (e) {
        fail(`failed to evaluate hardware-group type list: ${e.message}`);
    }
    // Sanity: the block guarded by this condition must actually be the
    // "Hardware Setup" group, not some other list.
    const hwGroupBodyStart = renderConfigBlock.indexOf(hwGroupMatch[0]);
    const hwGroupBody = renderConfigBlock.slice(hwGroupBodyStart, hwGroupBodyStart + 400);
    if (!hwGroupBody.includes('Hardware Setup')) {
        fail("the hardware-group type list guard did not lead to a 'Hardware Setup' group");
    }

    // The CordicOp accelerator group. There are two `if (t === 'CordicOp')`
    // guards in renderConfig: the paramGroups branch, and (later) this
    // "Accelerator / CORDIC" resource group. Anchor on the marker text and
    // require the nearest preceding guard to be the CordicOp one.
    const accelMarkerIdx = renderConfigBlock.indexOf('Accelerator / CORDIC');
    if (accelMarkerIdx < 0) fail("'Accelerator / CORDIC' marker text not found in renderConfig");
    const cordicGuardRe = /if \(t === 'CordicOp'\) \{/g;
    let lastGuardIdx = -1;
    let gcm;
    while ((gcm = cordicGuardRe.exec(renderConfigBlock)) !== null) {
        if (gcm.index < accelMarkerIdx) lastGuardIdx = gcm.index;
    }
    if (lastGuardIdx < 0) fail("no `if (t === 'CordicOp') {` guard precedes the 'Accelerator / CORDIC' group");
    if (accelMarkerIdx - lastGuardIdx > 1000) {
        fail("the nearest `if (t === 'CordicOp')` guard is too far from 'Accelerator / CORDIC' to be its group");
    }

    return {
        defaults: evaluated.defaults,
        descriptions: evaluated.descriptions,
        displayNames: evaluated.displayNames,
        categories: evaluated.categories,
        portsFn: evaluated.ports,
        getSelectOptions: evaluated.getSelectOptions,
        groupsByType,
        expectedTypeMap,
        hardwareGroupTypes,
    };
}

// ---------------------------------------------------------------------------
// graph_to_c.js extraction
// ---------------------------------------------------------------------------

function extractCodegenData(js) {
    const supportedMatch = requireMatch(/const supportedTypes = (\[[^\]]*\]);/, js, 'supportedTypes');
    let supportedTypes;
    try {
        // eslint-disable-next-line no-new-func
        supportedTypes = new Function('return ' + supportedMatch[1])();
    } catch (e) {
        fail(`failed to evaluate supportedTypes: ${e.message}`);
    }

    // CordicOp's per-operation output names, from nodeOutputNames()'s inner
    // switch. Any operation without an explicit `case` (i.e. only reachable
    // through `default: fail(...)`) is unsupported as a codegen source —
    // this is how 'atan2' is rejected.
    const cordicSwitchMatch = requireMatch(
        /case 'CordicOp':\s*switch \(node\.params\.operation\) \{([\s\S]*?)\n\s*\}/,
        js, "CordicOp's nodeOutputNames operation switch"
    );
    const cordicOutputsByOp = {};
    const caseRe = /case '(\w+)': return (\[[^\]]*\]);/g;
    let cm;
    let cordicCaseCount = 0;
    while ((cm = caseRe.exec(cordicSwitchMatch[1])) !== null) {
        cordicCaseCount++;
        let arr;
        try {
            // eslint-disable-next-line no-new-func
            arr = new Function('return ' + cm[2])();
        } catch (e) {
            fail(`failed to evaluate CordicOp case '${cm[1]}': ${e.message}`);
        }
        cordicOutputsByOp[cm[1]] = arr;
    }
    if (cordicCaseCount === 0) fail("no explicit 'case' entries found in CordicOp's operation switch");
    if (!/default:\s*fail\(/.test(cordicSwitchMatch[1])) {
        fail("CordicOp's operation switch no longer has a `default: fail(...)` fallback — atan2 rejection can't be confirmed");
    }

    // The sourceOutputs ternary chain in generate(): the definitive mapping
    // of "node type" -> "output ports codegen accepts as edge sources".
    const chainMatch = requireMatch(
        /const sourceOutputs = ([\s\S]*?);\s*\n\s*if \(!sourceOutputs\.includes/,
        js, 'sourceOutputs ternary chain in generate()'
    );
    const chainText = chainMatch[1];
    if (!/source\.type === 'CordicOp' \? nodeOutputNames\(source\)/.test(chainText)) {
        fail("sourceOutputs chain no longer routes CordicOp through nodeOutputNames(source)");
    }
    const codegenSources = {};
    const ternaryRe = /source\.type === '(\w+)' \? (\[[^\]]*\]|nodeOutputNames\(source\))/g;
    let tm;
    let ternaryCount = 0;
    while ((tm = ternaryRe.exec(chainText)) !== null) {
        ternaryCount++;
        if (tm[1] === 'CordicOp') continue; // handled via cordicOutputsByOp
        let arr;
        try {
            // eslint-disable-next-line no-new-func
            arr = new Function('return ' + tm[2])();
        } catch (e) {
            fail(`failed to evaluate sourceOutputs entry for '${tm[1]}': ${e.message}`);
        }
        codegenSources[tm[1]] = arr;
    }
    if (ternaryCount === 0) fail('sourceOutputs ternary chain did not match any entries');

    return { supportedTypes, cordicOutputsByOp, codegenSources };
}

// ---------------------------------------------------------------------------
// Assembly
// ---------------------------------------------------------------------------

function toDefaultsArray(obj) {
    return Object.keys(obj).map(key => ({ key, value: obj[key] }));
}

function toPortSpecArray(pairs) {
    return (pairs || []).map(([name, type]) => ({ name, type }));
}

function toOptionArray(options) {
    return options.map(x => (Array.isArray(x) ? { value: x[0], label: String(x[1]) } : { value: x, label: String(x) }));
}

function toFieldArray(fields, type, getSelectOptions) {
    return fields.map(([key, kind]) => {
        const field = { key, kind };
        if (kind === 'select') field.options = toOptionArray(getSelectOptions(key, type));
        return field;
    });
}

function buildNodeTypes(editorData, codegenData) {
    const { defaults, descriptions, displayNames, categories, portsFn, getSelectOptions, groupsByType, expectedTypeMap, hardwareGroupTypes } = editorData;

    const typeToCategory = {};
    for (const cat of categories) {
        for (const item of cat.items) typeToCategory[item] = cat.name;
    }

    const types = Object.keys(defaults).map(key => {
        const dynamic = key === 'CordicOp' || key === 'CustomCode';
        const staticPorts = portsFn(key, defaults[key]) || { in: [], out: [] };
        const category = typeToCategory[key];
        if (!category) fail(`node type '${key}' is not listed in any palette category`);

        const groups = (groupsByType[key] || []).map(g => ({
            name: g.name,
            fields: toFieldArray(g.fields, key, getSelectOptions),
        }));

        const hardwareResourceTypes = key === 'CordicOp'
            ? ['accelerator']
            : (hardwareGroupTypes.includes(key) ? (expectedTypeMap[key] || []) : []);

        return {
            key,
            displayName: displayNames[key] || key,
            description: descriptions[key] || '',
            category,
            defaults: toDefaultsArray(defaults[key]),
            ports: {
                dynamic,
                inputs: toPortSpecArray(staticPorts.in),
                outputs: toPortSpecArray(staticPorts.out),
            },
            inspectorGroups: groups,
            hardwareResourceTypes,
            special: {
                customCode: key === 'CustomCode',
                accelerator: key === 'CordicOp',
            },
        };
    });

    const categoriesOut = categories.map(c => {
        const out = { name: c.name, items: c.items.slice() };
        if (c.placeholder) out.placeholder = c.placeholder;
        return out;
    });

    // codegenSources: per type, the ports codegen accepts as edge sources.
    // CordicOp is operation-dependent; everything else is a flat list.
    // Types in codegenSupportedTypes but absent from the ternary chain (and
    // not CordicOp) generate no usable source ports at all (e.g.
    // ActuatorOutput, Publish, CustomCode) -> empty array.
    const codegenSourcesOut = {};
    for (const t of codegenData.supportedTypes) {
        if (t === 'CordicOp') {
            codegenSourcesOut[t] = { byOperation: codegenData.cordicOutputsByOp };
        } else {
            codegenSourcesOut[t] = { ports: codegenData.codegenSources[t] || [] };
        }
    }

    return {
        types,
        categories: categoriesOut,
        codegenSupportedTypes: codegenData.supportedTypes.slice(),
        codegenSources: codegenSourcesOut,
    };
}

function stableStringify(value) {
    return JSON.stringify(value, null, 2) + '\n';
}

function summarizeDiff(expected, actual) {
    const a = expected.split('\n');
    const b = actual.split('\n');
    const max = Math.max(a.length, b.length);
    const lines = [];
    let shown = 0;
    for (let i = 0; i < max && shown < 20; i++) {
        if (a[i] !== b[i]) {
            lines.push(`  line ${i + 1}:`);
            lines.push(`    - ${a[i] === undefined ? '<eof>' : a[i]}`);
            lines.push(`    + ${b[i] === undefined ? '<eof>' : b[i]}`);
            shown++;
        }
    }
    if (shown === 0) return '  (content differs but no line-level diff found)';
    return lines.join('\n');
}

function main() {
    const opts = parseArgs(process.argv.slice(2));

    if (!fs.existsSync(opts.html)) fail(`html source not found: ${opts.html}`);
    if (!fs.existsSync(opts.codegen)) fail(`codegen source not found: ${opts.codegen}`);

    const html = fs.readFileSync(opts.html, 'utf8');
    const codegenJs = fs.readFileSync(opts.codegen, 'utf8');

    const editorData = extractEditorData(html);
    const codegenData = extractCodegenData(codegenJs);
    const result = buildNodeTypes(editorData, codegenData);
    const output = stableStringify(result);

    if (opts.check) {
        if (!fs.existsSync(opts.out)) {
            process.stderr.write(`node_types_drift: committed file not found: ${opts.out}\n`);
            process.exit(1);
        }
        const committed = fs.readFileSync(opts.out, 'utf8');
        if (committed !== output) {
            process.stderr.write('node_types_drift: node_types.json is out of date with the web source.\n');
            process.stderr.write(summarizeDiff(committed, output) + '\n');
            process.exit(1);
        }
        process.stdout.write('node_types.json is up to date.\n');
        process.exit(0);
    }

    fs.writeFileSync(opts.out, output);
    process.stdout.write(`Wrote ${opts.out}\n`);
}

main();
