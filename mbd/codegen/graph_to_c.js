#!/usr/bin/env node

/**
 * graph_to_c.js — HyprAccel MBD-T3
 *
 * Generate C for the SDK operations represented by an MBD graph.
 *
 * Usage: node graph_to_c.js <graph.json> <output.c>
 *
 * The current SDK surface supports CordicOp (sin, cos, and sincos) and
 * Publish. Other node types are deliberately rejected rather than producing
 * code that silently omits graph behaviour.
 */

const fs = require('fs');
const path = require('path');

const GRAPH_IDENTIFIER = /^[A-Za-z][A-Za-z0-9_-]*$/;
const C_IDENTIFIER = /^[A-Za-z_][A-Za-z0-9_]*$/;
const C_KEYWORDS = new Set([
    'auto', 'break', 'case', 'char', 'const', 'continue', 'default', 'do',
    'double', 'else', 'enum', 'extern', 'float', 'for', 'goto', 'if', 'int',
    'long', 'register', 'return', 'short', 'signed', 'sizeof', 'static',
    'struct', 'switch', 'typedef', 'union', 'unsigned', 'void', 'volatile', 'while'
]);

function fail(message) {
    throw new Error(message);
}

function cString(value) {
    return JSON.stringify(value);
}

function cIdentifier(value, context) {
    if (!GRAPH_IDENTIFIER.test(value)) {
        fail(`${context} '${value}' is not a valid graph identifier`);
    }
    const symbol = value.replace(/-/g, '_');
    if (!C_IDENTIFIER.test(symbol) || C_KEYWORDS.has(symbol)) {
        fail(`${context} '${value}' cannot be represented as a C identifier`);
    }
    return symbol;
}

function nodeOutputNames(node) {
    if (node.type !== 'CordicOp') return [];
    switch (node.params.operation) {
        case 'sin': return ['value'];
        case 'cos': return ['value'];
        case 'sincos': return ['sin', 'cos'];
        default: fail(`CordicOp '${node.id}' operation '${node.params.operation}' is not supported by the SDK`);
    }
}

function topologicalOrder(nodes, edges) {
    const byId = new Map(nodes.map(node => [node.id, node]));
    const indegree = new Map(nodes.map(node => [node.id, 0]));
    const next = new Map(nodes.map(node => [node.id, []]));
    for (const edge of edges) {
        next.get(edge.from.node).push(edge.to.node);
        indegree.set(edge.to.node, indegree.get(edge.to.node) + 1);
    }

    const ready = nodes.filter(node => indegree.get(node.id) === 0).map(node => node.id);
    const ordered = [];
    while (ready.length > 0) {
        const id = ready.shift();
        ordered.push(byId.get(id));
        for (const target of next.get(id)) {
            indegree.set(target, indegree.get(target) - 1);
            if (indegree.get(target) === 0) ready.push(target);
        }
    }
    if (ordered.length !== nodes.length) fail('graph contains a cycle');
    return ordered;
}

function generate(graph, sourceName) {
    if (graph.format !== 'hypraccel.mbd.graph' || graph.version !== 1) {
        fail('expected a hypraccel.mbd.graph version 1 graph');
    }
    if (!Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) fail('graph must contain nodes and edges arrays');

    const nodeById = new Map();
    const nodeSymbols = new Set();
    for (const node of graph.nodes) {
        if (!node || typeof node.id !== 'string' || !node.params) fail('each node must have an id and params');
        if (nodeById.has(node.id)) fail(`duplicate node id '${node.id}'`);
        const nodeSymbol = cIdentifier(node.id, 'node id');
        if (nodeSymbols.has(nodeSymbol)) fail(`node id '${node.id}' collides with another generated C symbol`);
        nodeSymbols.add(nodeSymbol);
        if (node.type !== 'CordicOp' && node.type !== 'Publish') {
            fail(`node '${node.id}' has unsupported type '${node.type}'; the current SDK codegen supports CordicOp and Publish`);
        }
        if (node.type === 'CordicOp') nodeOutputNames(node);
        nodeById.set(node.id, node);
    }

    const inbound = new Map();
    for (const edge of graph.edges) {
        if (!edge || !edge.from || !edge.to) fail('each edge must have from and to endpoints');
        const source = nodeById.get(edge.from.node);
        const target = nodeById.get(edge.to.node);
        if (!source || !target) fail('edge references a node that does not exist');
        if (source.type !== 'CordicOp' || !nodeOutputNames(source).includes(edge.from.port)) {
            fail(`edge source '${edge.from.node}.${edge.from.port}' is not a supported generated output`);
        }
        if (target.type !== 'Publish' || edge.to.port !== 'value') {
            fail(`edge destination '${edge.to.node}.${edge.to.port}' is not a supported generated input`);
        }
        const destination = `${edge.to.node}.${edge.to.port}`;
        if (inbound.has(destination)) fail(`input '${destination}' has more than one edge`);
        inbound.set(destination, { node: source, port: edge.from.port });
    }

    const inputs = new Map((graph.inputs || []).map(input => [input.name, input]));
    const inputSymbols = new Set();
    for (const input of inputs.values()) {
        const inputSymbol = cIdentifier(input.name, 'graph input');
        if (inputSymbols.has(inputSymbol)) fail(`graph input '${input.name}' collides with another generated C symbol`);
        inputSymbols.add(inputSymbol);
    }
    const bindings = new Map();
    for (const binding of (graph.metadata && graph.metadata.externalBindings) || []) {
        if (!binding || !binding.to || !inputs.has(binding.input)) fail('externalBinding references an undeclared graph input');
        const node = nodeById.get(binding.to.node);
        if (!node || node.type !== 'CordicOp' || binding.to.port !== 'angle_rad') {
            fail('externalBindings currently support only CordicOp.angle_rad');
        }
        if (inputs.get(binding.input).type !== 'number') fail(`graph input '${binding.input}' must have type number`);
        const destination = `${binding.to.node}.${binding.to.port}`;
        if (bindings.has(destination)) fail(`input '${destination}' has more than one external binding`);
        bindings.set(destination, binding.input);
    }

    const ordered = topologicalOrder(graph.nodes, graph.edges);
    const functionName = `hyp_graph_${cIdentifier(graph.id, 'graph id').replace(/-/g, '_')}_step`;
    const parameters = [...inputs.values()]
        .filter(input => input.type === 'number')
        .map(input => `float ${cIdentifier(input.name, 'graph input')}`);
    if (parameters.length !== inputs.size) fail('current codegen supports only number graph inputs');

    const lines = [
        '/*',
        ` * Generated by HyprAccel MBD-T3 graph_to_c.js from ${sourceName}.`,
        ' * Do not edit this file directly; edit the graph and regenerate.',
        ' */',
        '',
        '#include "hyprccel.h"',
        '',
        'int hyp_graph_init(void)',
        '{',
        '    return hyp_init();',
        '}',
        '',
        `void ${functionName}(${parameters.join(', ') || 'void'})`,
        '{'
    ];

    const boundInputs = new Set(bindings.values());
    for (const input of inputs.values()) {
        if (!boundInputs.has(input.name)) {
            lines.push(`    (void)${cIdentifier(input.name, 'graph input')};`);
        }
    }

    const usedOutputs = new Set([...inbound.values()].map(edge => `${edge.node.id}.${edge.port}`));

    for (const node of ordered) {
        if (node.type === 'CordicOp') {
            const input = bindings.get(`${node.id}.angle_rad`);
            if (!input) fail(`CordicOp '${node.id}' requires an external binding for angle_rad`);
            const nodeName = cIdentifier(node.id, 'node id');
            const args = `${nodeName}_args`;
            const target = node.params.implementation === 'hardware' ? 'HYP_TARGET_HARDWARE' : 'HYP_TARGET_SOFTWARE';
            lines.push(`    hyp_cordic_args_t ${args} = {0};`);
            lines.push(`    ${args}.angle_degrees = ${cIdentifier(input, 'graph input')} * (180.0f / 3.14159265358979323846f);`);
            lines.push(`    hyp_route(HYP_OP_CORDIC_SINCOS, ${target});`);
            lines.push(`    hyp_compute(HYP_OP_CORDIC_SINCOS, &${args});`);
            for (const output of nodeOutputNames(node)) {
                if (!usedOutputs.has(`${node.id}.${output}`)) continue;
                const field = output === 'sin' || node.params.operation === 'sin' ? 'out_sin' : 'out_cos';
                lines.push(`    float ${nodeName}_${output} = ${args}.${field};`);
            }
        } else {
            const edge = inbound.get(`${node.id}.value`);
            if (!edge) fail(`Publish '${node.id}' requires a value input edge`);
            const value = `${cIdentifier(edge.node.id, 'node id')}_${edge.port}`;
            lines.push(`    hyp_publish(${cString(node.params.topic)}, &${value}, (uint32_t)sizeof(${value}));`);
        }
    }
    lines.push('}', '');
    return lines.join('\n');
}

function main() {
    const args = process.argv.slice(2);
    if (args.length !== 2) {
        console.error('Usage: node graph_to_c.js <graph.json> <output.c>');
        process.exit(1);
    }
    const [graphPath, outputPath] = args;
    let graph;
    try {
        graph = JSON.parse(fs.readFileSync(graphPath, 'utf8'));
        const output = generate(graph, path.basename(graphPath));
        fs.mkdirSync(path.dirname(outputPath), { recursive: true });
        fs.writeFileSync(outputPath, output, 'utf8');
        console.log(`Generated ${outputPath}`);
    } catch (error) {
        console.error(`graph_to_c: ${error.message}`);
        process.exit(1);
    }
}

main();
