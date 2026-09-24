#!/usr/bin/env node
'use strict';

// Proves the node_types_drift ctest actually bites: feeds the extractor
// deliberately mutated copies of graph_editor.html and asserts `--check`
// fails against the real committed node_types.json. See
// desktop/docs/mbd_graph_contract.md §6 D1.
//
// Usage: node extract_node_types_selftest.js <extract_node_types.js> <real graph_to_c.js> <committed node_types.json>

const { execFileSync } = require('child_process');
const path = require('path');

function fail(message) {
    process.stderr.write('extract_node_types_selftest: FAIL: ' + message + '\n');
    process.exit(1);
}

function runCheck(extractorPath, htmlPath, codegenPath, outPath) {
    try {
        execFileSync(process.execPath, [extractorPath, '--check', '--html', htmlPath, '--codegen', codegenPath, '--out', outPath], { stdio: 'pipe' });
        return { exitCode: 0, stderr: '' };
    } catch (e) {
        return { exitCode: e.status === null ? -1 : e.status, stderr: (e.stderr || Buffer.alloc(0)).toString('utf8') };
    }
}

function main() {
    const [, , extractorPath, codegenPath, outPath] = process.argv;
    if (!extractorPath || !codegenPath || !outPath) {
        process.stderr.write('usage: extract_node_types_selftest.js <extract_node_types.js> <graph_to_c.js> <node_types.json>\n');
        process.exit(2);
    }
    const fixturesDir = path.join(__dirname, 'fixtures');

    // 1. A changed default value must be caught as a content diff (exit 1).
    const r1 = runCheck(extractorPath, path.join(fixturesDir, 'mutated_default_editor.html'), codegenPath, outPath);
    if (r1.exitCode !== 1) fail(`mutated default value: expected --check to exit 1, got ${r1.exitCode}\n${r1.stderr}`);
    if (!/out of date/.test(r1.stderr)) fail(`mutated default value: expected a readable drift message, got: ${r1.stderr}`);

    // 2. An added node type with no inspector-group branch must be caught as
    // a structural pattern failure (exit 2, loud, not a silent partial write).
    const r2 = runCheck(extractorPath, path.join(fixturesDir, 'mutated_new_node_editor.html'), codegenPath, outPath);
    if (r2.exitCode !== 2) fail(`mutated new node type: expected --check to exit 2, got ${r2.exitCode}\n${r2.stderr}`);

    // 3. Sanity: the real, unmutated source must still pass --check, proving
    // the fixtures above are the only source of the failures above.
    const repoRoot = path.join(__dirname, '..', '..', '..', '..', '..');
    const realHtml = path.join(repoRoot, 'mbd', 'editor', 'src', 'graph_editor.html');
    const r3 = runCheck(extractorPath, realHtml, codegenPath, outPath);
    if (r3.exitCode !== 0) fail(`real graph_editor.html: expected --check to pass, got ${r3.exitCode}\n${r3.stderr}`);

    process.stdout.write('extract_node_types_selftest: OK (drift test bites on both a value change and a structural change)\n');
}

main();
