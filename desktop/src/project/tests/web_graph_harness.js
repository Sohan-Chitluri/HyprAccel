#!/usr/bin/env node
// Test-only oracle for graph_store_test.cpp / graph_codegen_runner_test.cpp.
// NOT part of the frozen web app: it extracts pure functions verbatim from
// mbd/editor/src/graph_editor.html (never edited) and evaluates them with
// `new Function`, the same technique used elsewhere in this repo's own test
// suite (see mbd/editor/test/*.test.js) to test the editor's logic outside a
// browser. It reimplements server.js's tiny graphDisplayName() helper
// (5 lines, copied verbatim below) because writeProjectGraph applies it to
// the name toGraph() produces before writing; everything else is the web's
// own code.
//
// Usage:
//   node web_graph_harness.js toGraph <graphDoc.json> [targetBoard]
//     -> prints the exact bytes GraphStore::save() must reproduce:
//        writeProjectGraph(toGraph(doc)) serialized with
//        JSON.stringify(graph, null, 2) + "\n"
//   node web_graph_harness.js loadDocument <graphDoc.json>
//     -> prints {nodes, edges} = {graphNodesFromDocument(doc), graphEdgesFromDocument(doc)}
//   node web_graph_harness.js graphIdFromName <name>
//     -> prints graphIdFromName(name)
//   node web_graph_harness.js selectValue <jsonValue> <jsonOptionsArray>
//     -> prints the index of the option React 17's controlled <select>
//        would mark selected for jsonValue, using the documented match rule
//        ('' + value === '' + optionValue), or -1.

const fs = require('fs');
const path = require('path');

const htmlPath = process.env.GRAPH_EDITOR_HTML || path.join(__dirname, '../../../../mbd/editor/src/graph_editor.html');
const html = fs.readFileSync(htmlPath, 'utf8');

function extractFunction(src, name) {
    const marker = `function ${name}(`;
    const start = src.indexOf(marker);
    if (start === -1) throw new Error(`function ${name} not found in graph_editor.html`);
    let i = src.indexOf('{', start);
    let depth = 0;
    for (; i < src.length; i++) {
        if (src[i] === '{') depth++;
        else if (src[i] === '}') { depth--; if (depth === 0) { i++; break; } }
    }
    return src.slice(start, i);
}

function extractConst(src, name) {
    const marker = `const ${name} = `;
    const start = src.indexOf(marker);
    if (start === -1) throw new Error(`const ${name} not found in graph_editor.html`);
    const end = src.indexOf(';\n', start);
    if (end === -1) throw new Error(`terminator for const ${name} not found`);
    return src.slice(start, end + 1);
}

const displayNamesSrc = extractConst(html, 'displayNames');
const toGraphSrc = extractFunction(html, 'toGraph');
const graphNodesFromDocumentSrc = extractFunction(html, 'graphNodesFromDocument');
const graphEdgesFromDocumentSrc = extractFunction(html, 'graphEdgesFromDocument');
const graphIdFromNameSrc = extractFunction(html, 'graphIdFromName');

const body = `
${displayNamesSrc}
function clone(value) { return JSON.parse(JSON.stringify(value)); }
let graphNodes = state.graphNodes, graphEdges = state.graphEdges;
let activeGraphId = state.activeGraphId, activeGraphName = state.activeGraphName;
let currentHardware = state.currentHardware;
${toGraphSrc}
${graphNodesFromDocumentSrc}
${graphEdgesFromDocumentSrc}
${graphIdFromNameSrc}
return { toGraph, graphNodesFromDocument, graphEdgesFromDocument, graphIdFromName };
`;

function makeApi(state) {
    // eslint-disable-next-line no-new-func
    const factory = new Function('state', body);
    return factory(state || {});
}

// server.js:206 graphDisplayName, copied verbatim (frozen server.js is not
// executed here; this is the harness's own oracle copy of a 5-line pure
// function, kept byte-identical to the source it mirrors).
function graphDisplayName(name, fallback) {
    if (name == null || name === '') return fallback;
    if (typeof name !== 'string' || !name.trim() || name.trim().length > 120) {
        throw new Error('Graph name must be a non-empty string no longer than 120 characters.');
    }
    return name.trim();
}

function main() {
    const [, , mode, ...args] = process.argv;
    if (mode === 'toGraph') {
        const doc = JSON.parse(fs.readFileSync(args[0], 'utf8'));
        const targetBoard = args[1] || null;
        const api = makeApi({
            graphNodes: [],
            graphEdges: [],
            activeGraphId: doc.id,
            activeGraphName: doc.name,
            currentHardware: targetBoard ? { board: targetBoard } : null,
        });
        // toGraph() reads graphNodes/graphEdges (React Flow's runtime shape,
        // {id,type,position,data:{nodeType,label,params}} / {id,source,sourceHandle,target,targetHandle}),
        // not the raw document nodes/edges arrays. Load through
        // graphNodesFromDocument/graphEdgesFromDocument first, exactly as
        // the editor does after a fetch.
        const api2 = makeApi({
            graphNodes: api.graphNodesFromDocument(doc),
            graphEdges: api.graphEdgesFromDocument(doc),
            activeGraphId: doc.id,
            activeGraphName: doc.name,
            currentHardware: targetBoard ? { board: targetBoard } : null,
        });
        const graph = api2.toGraph();
        graph.name = graphDisplayName(graph.name, graph.id);
        process.stdout.write(JSON.stringify(graph, null, 2) + '\n');
        return;
    }
    if (mode === 'loadDocument') {
        const doc = JSON.parse(fs.readFileSync(args[0], 'utf8'));
        const api = makeApi({});
        const out = {
            nodes: api.graphNodesFromDocument(doc),
            edges: api.graphEdgesFromDocument(doc),
        };
        process.stdout.write(JSON.stringify(out, null, 2) + '\n');
        return;
    }
    if (mode === 'graphIdFromName') {
        const api = makeApi({});
        process.stdout.write(api.graphIdFromName(args[0]) + '\n');
        return;
    }
    if (mode === 'selectValue') {
        // React 17's ReactDOMSelect controlled-value matching
        // (react-dom/cjs/react-dom.development.js, updateOptions): for a
        // single-select, the option whose `'' + option.value === '' + value`
        // is marked selected. This evaluates that exact documented rule
        // without a DOM/jsdom render (neither react-dom nor jsdom is present
        // in mbd/editor/node_modules — see the report for what this does
        // and does not prove).
        const value = JSON.parse(args[0]);
        const options = JSON.parse(args[1]);
        let selectedIndex = -1;
        for (let i = 0; i < options.length; i++) {
            if (('' + value) === ('' + options[i])) { selectedIndex = i; break; }
        }
        process.stdout.write(String(selectedIndex) + '\n');
        return;
    }
    process.stderr.write('Usage: web_graph_harness.js <toGraph|loadDocument|graphIdFromName|selectValue> ...\n');
    process.exit(1);
}

main();
