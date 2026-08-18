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
    'long', 'register', 'return', 'short', 'signed', 'sizeof', 'static', 'struct',
    'switch', 'typedef', 'union', 'unsigned', 'void', 'volatile', 'while'
]);

function fail(message) {
    throw new Error(message);
}

function cString(value) {
    return JSON.stringify(value);
}

function hardwareResource(node, expectedType) {
    const resourceId = node.params && node.params.hardwareResource;
    if (typeof resourceId !== 'string' || !resourceId) {
        fail(`${node.type} '${node.id}' requires a hardwareResource parameter`);
    }
    const match = /^(gpio|uart|spi|i2c|pwm|adc|accelerator|encoder|motor)\.([A-Za-z0-9_-]+)$/.exec(resourceId);
    if (!match) {
        fail(`${node.type} '${node.id}' has invalid canonical hardware resource '${resourceId}'`);
    }
    const expectedTypes = Array.isArray(expectedType) ? expectedType : [expectedType];
    if (!expectedTypes.includes(match[1])) {
        fail(`${node.type} '${node.id}' requires a ${expectedTypes.join(' or ')} hardware resource, got '${resourceId}'`);
    }
    return resourceId;
}

function numberLiteral(value, context) {
    if (typeof value !== 'number' || !Number.isFinite(value)) fail(`${context} must be a finite number`);
    return `${Number.isInteger(value) ? `${value}.0` : value}f`;
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
    switch (node.type) {
        case 'CordicOp':
            switch (node.params.operation) {
                case 'sin': return ['value'];
                case 'cos': return ['value'];
                case 'sincos': return ['sin', 'cos'];
                default: fail(`CordicOp '${node.id}' operation '${node.params.operation}' is not supported by the SDK`);
            }
        case 'SensorInput':
            return ['value', 'timestamp_us', 'valid'];
        case 'Constant':
            return ['value'];
        case 'Add':
        case 'Subtract':
        case 'Multiply':
        case 'Gain':
            return ['value'];
        case 'Compare':
            return ['result'];
        case 'Saturation':
            return ['value'];
        case 'Switch':
            return ['value'];
        case 'ControlLoop':
            return ['command', 'error'];
        case 'GPIOInput':
            return ['value'];
        case 'ADCInput':
            return ['value'];
        case 'UARTInput':
            return ['value', 'valid'];
        case 'UARTOutput':
            return ['sent'];
        case 'EncoderInput':
            return ['position', 'velocity', 'valid'];
        case 'MotorOutput':
            return ['applied', 'active'];
        case 'WheelSpeed':
            return ['speed'];
        case 'DifferentialDrive':
            return ['left_cmd', 'right_cmd'];
        default:
            return [];
    }
}

function nodeInputNames(node) {
    switch (node.type) {
        case 'CordicOp':
            if (node.params.operation === 'atan2') return ['y', 'x'];
            return ['angle_rad'];
        case 'Publish':
            return ['value', 'timestamp_us'];
        case 'ActuatorOutput':
            return ['command', 'enable'];
        case 'Add':
        case 'Subtract':
        case 'Multiply':
            return ['a', 'b'];
        case 'Gain':
            return ['input'];
        case 'Compare':
            return ['input'];
        case 'Saturation':
            return ['input'];
        case 'Switch':
            return ['condition', 'true_value', 'false_value'];
        case 'ControlLoop':
            return ['setpoint', 'measurement', 'enable'];
        case 'PWMOutput':
            return ['value', 'enable'];
        case 'UARTInput':
            return [];
        case 'UARTOutput':
            return ['data'];
        case 'EncoderInput':
            return [];
        case 'MotorOutput':
            return ['command', 'enable'];
        case 'WheelSpeed':
            return ['encoder_count'];
        case 'DifferentialDrive':
            return ['linear_velocity', 'angular_velocity'];
        default:
            return [];
    }
}

function cordicTarget(node) {
    // These names intentionally match hyp_target_t in sdk/include/hyprccel.h
    // and are consumed unchanged by CORE-T7's hyp_route() implementation.
    switch (node.params.implementation || 'auto') {
        case 'auto':
        case 'software':
            return 'HYP_TARGET_SOFTWARE';
        case 'hardware':
            return 'HYP_TARGET_HARDWARE';
        default:
            fail(`CordicOp '${node.id}' has unsupported implementation '${node.params.implementation}'`);
    }
}

function sensorOutputNames(node) {
    if (node.type !== 'SensorInput') return [];
    // SensorInput outputs: value, timestamp_us, valid
    return ['value', 'timestamp_us', 'valid'];
}

function actuatorInputNames(node) {
    if (node.type !== 'ActuatorOutput') return [];
    // ActuatorOutput inputs: command, enable
    return ['command', 'enable'];
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
    if (!graph || graph.format !== 'hypraccel.mbd.graph' || graph.version !== 1) {
        fail('expected a hypraccel.mbd.graph version 1 graph');
    }
    if (!Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) fail('graph must contain nodes and edges arrays');
    const nodeById = new Map();
    const nodeSymbols = new Set();
    const supportedTypes = ['CordicOp', 'Publish', 'SensorInput', 'ActuatorOutput', 'Constant', 'Add', 'Subtract', 'Multiply', 'Gain', 'Compare', 'Saturation', 'Switch', 'ControlLoop', 'GPIOInput', 'ADCInput', 'PWMOutput', 'UARTInput', 'UARTOutput', 'EncoderInput', 'MotorOutput', 'WheelSpeed', 'DifferentialDrive'];

    for (const node of graph.nodes) {
        if (!node || typeof node.id !== 'string' || !node.params || nodeById.has(node.id)) {
            fail('each node must have a unique id and params object');
        }
        const nodeSymbol = cIdentifier(node.id, 'node id');
        if (nodeSymbols.has(nodeSymbol)) fail(`node id '${node.id}' collides with another generated C symbol`);
        nodeSymbols.add(nodeSymbol);
        if (!supportedTypes.includes(node.type)) {
            fail(`node '${node.id}' has unsupported type '${node.type}'`);
        }
        if (node.type === 'CordicOp') nodeOutputNames(node);
        if (node.type === 'GPIOInput') hardwareResource(node, 'gpio');
        if (node.type === 'ADCInput') hardwareResource(node, 'adc');
        if (node.type === 'PWMOutput') hardwareResource(node, 'pwm');
        if (node.type === 'UARTInput') hardwareResource(node, 'uart');
        if (node.type === 'UARTOutput') hardwareResource(node, 'uart');
        if (node.type === 'EncoderInput') hardwareResource(node, 'encoder');
        if (node.type === 'MotorOutput') hardwareResource(node, 'motor');
        nodeById.set(node.id, node);
    }

    const inbound = new Map();
    for (const edge of graph.edges) {
        if (!edge || !edge.from || !edge.to) fail('each edge must have from and to endpoints');
        const source = nodeById.get(edge.from.node);
        const target = nodeById.get(edge.to.node);
        if (!source || !target) fail('edge references a node that does not exist');

        // Validate source port
        const sourceOutputs = source.type === 'CordicOp' ? nodeOutputNames(source) :
                              source.type === 'SensorInput' ? sensorOutputNames(source) :
                              source.type === 'Constant' ? ['value'] :
                              source.type === 'Add' ? ['value'] :
                              source.type === 'Subtract' ? ['value'] :
                              source.type === 'Multiply' ? ['value'] :
                              source.type === 'Gain' ? ['value'] :
                              source.type === 'Compare' ? ['result'] :
                              source.type === 'Saturation' ? ['value'] :
                              source.type === 'Switch' ? ['value'] :
                              source.type === 'ControlLoop' ? ['command', 'error'] :
                              source.type === 'GPIOInput' ? ['value'] :
                              source.type === 'ADCInput' ? ['value'] :
                              source.type === 'UARTInput' ? ['value', 'valid'] :
                              source.type === 'EncoderInput' ? ['position', 'velocity', 'valid'] :
                              source.type === 'MotorOutput' ? ['applied', 'active'] :
                              source.type === 'WheelSpeed' ? ['speed'] :
                              source.type === 'DifferentialDrive' ? ['left_cmd', 'right_cmd'] :
                              source.type === 'PWMOutput' ? ['applied', 'active'] :
                              source.type === 'UARTOutput' ? ['sent'] : [];
        if (!sourceOutputs.includes(edge.from.port)) {
            fail(`edge source '${edge.from.node}.${edge.from.port}' is not a supported generated output`);
        }

        // Validate target port
        const targetInputs = nodeInputNames(target);
        if (!targetInputs.includes(edge.to.port)) {
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
        const nodeName = cIdentifier(node.id, 'node id');

        if (node.type === 'CordicOp') {
            const input = bindings.get(`${node.id}.angle_rad`);
            if (!input) fail(`CordicOp '${node.id}' requires an external binding for angle_rad`);
            const args = `${nodeName}_args`;
            const target = cordicTarget(node);
            lines.push(`    hyp_cordic_args_t ${args} = {0};`);
            lines.push(`    ${args}.angle_degrees = ${cIdentifier(input, 'graph input')} * (180.0f / 3.14159265358979323846f);`);
            lines.push(`    hyp_route(HYP_OP_CORDIC_SINCOS, ${target});`);
            lines.push(`    hyp_compute(HYP_OP_CORDIC_SINCOS, &${args});`);
            for (const output of nodeOutputNames(node)) {
                if (!usedOutputs.has(`${node.id}.${output}`)) continue;
                const field = output === 'sin' || node.params.operation === 'sin' ? 'out_sin' : 'out_cos';
                lines.push(`    float ${nodeName}_${output} = ${args}.${field};`);
            }
        } else if (node.type === 'GPIOInput') {
            const resourceId = hardwareResource(node, 'gpio');
            const valueUsed = usedOutputs.has(`${node.id}.value`);
            if (valueUsed) {
                lines.push(`    uint8_t ${nodeName}_value = 0;`);
                lines.push(`    if (hyp_sensor_read(${cString(resourceId)}, &${nodeName}_value, sizeof(${nodeName}_value)) == HYP_RUNTIME_OK) {`);
                if (node.params.invert) lines.push(`        ${nodeName}_value = (uint8_t)!${nodeName}_value;`);
                lines.push('    }');
            }

        } else if (node.type === 'ADCInput') {
            const resourceId = hardwareResource(node, 'adc');
            if (node.params.minValue !== undefined && node.params.maxValue !== undefined && node.params.maxValue < node.params.minValue) {
                fail(`ADCInput '${node.id}' maxValue must be at least minValue`);
            }
            const valueUsed = usedOutputs.has(`${node.id}.value`);
            if (valueUsed) {
                lines.push(`    float ${nodeName}_value = 0.0f;`);
                lines.push(`    hyp_sensor_read(${cString(resourceId)}, &${nodeName}_value, sizeof(${nodeName}_value));`);
                if (node.params.minValue !== undefined) lines.push(`    if (${nodeName}_value < ${numberLiteral(node.params.minValue, `${node.type} '${node.id}' minValue`)}) ${nodeName}_value = ${numberLiteral(node.params.minValue, `${node.type} '${node.id}' minValue`)};`);
                if (node.params.maxValue !== undefined) lines.push(`    if (${nodeName}_value > ${numberLiteral(node.params.maxValue, `${node.type} '${node.id}' maxValue`)}) ${nodeName}_value = ${numberLiteral(node.params.maxValue, `${node.type} '${node.id}' maxValue`)};`);
            }

        } else if (node.type === 'SensorInput') {
            const resourceId = hardwareResource(node, ['gpio', 'adc', 'uart']);
            if (node.params.valueType !== 'number') {
                fail(`SensorInput '${node.id}' valueType '${node.params.valueType}' is not supported by the scalar hardware runtime`);
            }

            // Check if sensor outputs are actually used
            const valueUsed = usedOutputs.has(`${node.id}.value`);
            const timestampUsed = usedOutputs.has(`${node.id}.timestamp_us`);
            const validUsed = usedOutputs.has(`${node.id}.valid`);
            const resultNeeded = timestampUsed || validUsed;
            const readNeeded = valueUsed || resultNeeded;

            // A status-only consumer still requires a read so that `valid` and
            // `timestamp_us` describe this step's runtime operation.
            if (readNeeded) {
                lines.push(`    float ${nodeName}_value = 0.0f;`);
            }
            if (timestampUsed) {
                lines.push(`    uint32_t ${nodeName}_timestamp_us = 0;`);
            }
            if (validUsed) {
                lines.push(`    uint8_t ${nodeName}_valid = 0;`);
            }

            // The SDK owns peripheral and clock access; generated C only passes
            // the canonical resource ID and scalar payload storage.
            if (readNeeded) {
                if (resultNeeded) {
                    lines.push(`    int ${nodeName}_result = hyp_sensor_read(${cString(resourceId)}, &${nodeName}_value, sizeof(${nodeName}_value));`);
                    lines.push(`    if (${nodeName}_result == 0) {`);
                    if (validUsed) {
                        lines.push(`        ${nodeName}_valid = 1;`);
                    }
                    if (timestampUsed) {
                        lines.push(`        ${nodeName}_timestamp_us = hyp_timestamp_us();`);
                    }
                    lines.push(`    }`);
                } else {
                    lines.push(`    hyp_sensor_read(${cString(resourceId)}, &${nodeName}_value, sizeof(${nodeName}_value));`);
                }
            }
            if (readNeeded && !valueUsed) lines.push(`    (void)${nodeName}_value;`);

        } else if (node.type === 'PWMOutput') {
            const inputEdge = inbound.get(`${node.id}.value`);
            if (!inputEdge) fail(`PWMOutput '${node.id}' requires a 'value' input edge`);
            const inputValue = `${cIdentifier(inputEdge.node.id, 'node id')}_${inputEdge.port}`;
            const resourceId = hardwareResource(node, 'pwm');
            const min = numberLiteral(node.params.min, `PWMOutput '${node.id}' min`);
            const max = numberLiteral(node.params.max, `PWMOutput '${node.id}' max`);
            if (node.params.max <= node.params.min) fail(`PWMOutput '${node.id}' max must be greater than min`);
            const safe = numberLiteral(node.params.safeValue === undefined ? node.params.min : node.params.safeValue, `PWMOutput '${node.id}' safeValue`);
            lines.push(`    float ${nodeName}_applied = ${inputValue};`);
            lines.push(`    if (${nodeName}_applied < ${min}) ${nodeName}_applied = ${min};`);
            lines.push(`    if (${nodeName}_applied > ${max}) ${nodeName}_applied = ${max};`);
            lines.push(`    float ${nodeName}_duty = (${nodeName}_applied - ${min}) / (${max} - ${min});`);
            lines.push(`    float ${nodeName}_safe_value = ${safe};`);
            lines.push(`    if (${nodeName}_safe_value < ${min}) ${nodeName}_safe_value = ${min};`);
            lines.push(`    if (${nodeName}_safe_value > ${max}) ${nodeName}_safe_value = ${max};`);
            lines.push(`    uint8_t ${nodeName}_active = 0;`);
            const enableEdge = inbound.get(`${node.id}.enable`);
            if (enableEdge) {
                const enableValue = `${cIdentifier(enableEdge.node.id, 'node id')}_${enableEdge.port}`;
                lines.push(`    if (${enableValue}) ${nodeName}_active = (hyp_actuator_write(${cString(resourceId)}, &${nodeName}_duty, sizeof(${nodeName}_duty)) == HYP_RUNTIME_OK);`);
                lines.push(`    else { float ${nodeName}_safe_duty = (${nodeName}_safe_value - ${min}) / (${max} - ${min}); hyp_actuator_write(${cString(resourceId)}, &${nodeName}_safe_duty, sizeof(${nodeName}_safe_duty)); }`);
            } else {
                lines.push(`    ${nodeName}_active = (hyp_actuator_write(${cString(resourceId)}, &${nodeName}_duty, sizeof(${nodeName}_duty)) == HYP_RUNTIME_OK);`);
            }
            if (!usedOutputs.has(`${node.id}.applied`)) lines.push(`    (void)${nodeName}_applied;`);
            if (!enableEdge || !usedOutputs.has(`${node.id}.active`)) lines.push(`    (void)${nodeName}_safe_value;`);
            if (!usedOutputs.has(`${node.id}.active`)) lines.push(`    (void)${nodeName}_active;`);

        } else if (node.type === 'ActuatorOutput') {
            const edge = inbound.get(`${node.id}.command`);
            if (!edge) fail(`ActuatorOutput '${node.id}' requires a command input edge`);
            const value = `${cIdentifier(edge.node.id, 'node id')}_${edge.port}`;

            const resourceId = hardwareResource(node, ['gpio', 'pwm', 'uart']);

            // Optional enable input
            const enableEdge = inbound.get(`${node.id}.enable`);
            if (enableEdge) {
                const enableVal = `${cIdentifier(enableEdge.node.id, 'node id')}_${enableEdge.port}`;
                lines.push(`    if (${enableVal}) {`);
                lines.push(`        hyp_actuator_write(${cString(resourceId)}, &${value}, sizeof(${value}));`);
                lines.push(`    }`);
            } else {
                lines.push(`    hyp_actuator_write(${cString(resourceId)}, &${value}, sizeof(${value}));`);
            }

        } else if (node.type === 'Publish') {
            const edge = inbound.get(`${node.id}.value`);
            if (!edge) fail(`Publish '${node.id}' requires a value input edge`);
            const value = `${cIdentifier(edge.node.id, 'node id')}_${edge.port}`;
            const timestampEdge = inbound.get(`${node.id}.timestamp_us`);
            if (timestampEdge) {
                const timestamp = `${cIdentifier(timestampEdge.node.id, 'node id')}_${timestampEdge.port}`;
                // The current SDK publication contract has no timestamp field.
                // Preserve the graph dependency and avoid silently bypassing the
                // value source while retaining its source-assigned timestamp.
                lines.push(`    (void)${timestamp};`);
            }
            lines.push(`    hyp_publish(${cString(node.params.topic)}, &${value}, (uint32_t)sizeof(${value}));`);

        } else if (node.type === 'Constant') {
            const value = node.params.value;
            lines.push(`    float ${nodeName}_value = ${numberLiteral(value, `Constant '${node.id}' value`)};`);

        } else if (node.type === 'Add') {
            const aEdge = inbound.get(`${node.id}.a`);
            const bEdge = inbound.get(`${node.id}.b`);
            if (!aEdge || !bEdge) fail(`Add '${node.id}' requires both 'a' and 'b' input edges`);
            const aVal = `${cIdentifier(aEdge.node.id, 'node id')}_${aEdge.port}`;
            const bVal = `${cIdentifier(bEdge.node.id, 'node id')}_${bEdge.port}`;
            lines.push(`    float ${nodeName}_value = ${aVal} + ${bVal};`);

        } else if (node.type === 'Subtract') {
            const aEdge = inbound.get(`${node.id}.a`);
            const bEdge = inbound.get(`${node.id}.b`);
            if (!aEdge || !bEdge) fail(`Subtract '${node.id}' requires both 'a' and 'b' input edges`);
            const aVal = `${cIdentifier(aEdge.node.id, 'node id')}_${aEdge.port}`;
            const bVal = `${cIdentifier(bEdge.node.id, 'node id')}_${bEdge.port}`;
            lines.push(`    float ${nodeName}_value = ${aVal} - ${bVal};`);

        } else if (node.type === 'Multiply') {
            const aEdge = inbound.get(`${node.id}.a`);
            const bEdge = inbound.get(`${node.id}.b`);
            if (!aEdge || !bEdge) fail(`Multiply '${node.id}' requires both 'a' and 'b' input edges`);
            const aVal = `${cIdentifier(aEdge.node.id, 'node id')}_${aEdge.port}`;
            const bVal = `${cIdentifier(bEdge.node.id, 'node id')}_${bEdge.port}`;
            lines.push(`    float ${nodeName}_value = ${aVal} * ${bVal};`);

        } else if (node.type === 'Gain') {
            const inputEdge = inbound.get(`${node.id}.input`);
            if (!inputEdge) fail(`Gain '${node.id}' requires an 'input' edge`);
            const inputVal = `${cIdentifier(inputEdge.node.id, 'node id')}_${inputEdge.port}`;
            const gain = node.params.gain;
            lines.push(`    float ${nodeName}_value = ${inputVal} * ${numberLiteral(gain, `Gain '${node.id}' gain`)};`);

        } else if (node.type === 'Compare') {
            const inputEdge = inbound.get(`${node.id}.input`);
            if (!inputEdge) fail(`Compare '${node.id}' requires an 'input' edge`);
            const inputVal = `${cIdentifier(inputEdge.node.id, 'node id')}_${inputEdge.port}`;
            const op = node.params.operation;
            const threshold = node.params.threshold;
            let cmpOp;
            switch (op) {
                case 'gt': cmpOp = '>'; break;
                case 'lt': cmpOp = '<'; break;
                case 'ge': cmpOp = '>='; break;
                case 'le': cmpOp = '<='; break;
                case 'eq': cmpOp = '=='; break;
                case 'ne': cmpOp = '!='; break;
                default: fail(`Compare '${node.id}' has unsupported operation '${op}'`);
            }
            lines.push(`    uint8_t ${nodeName}_result = (${inputVal} ${cmpOp} ${numberLiteral(threshold, `Compare '${node.id}' threshold`)}) ? 1 : 0;`);

        } else if (node.type === 'Saturation') {
            const inputEdge = inbound.get(`${node.id}.input`);
            if (!inputEdge) fail(`Saturation '${node.id}' requires an 'input' edge`);
            const inputVal = `${cIdentifier(inputEdge.node.id, 'node id')}_${inputEdge.port}`;
            const min = node.params.min;
            const max = node.params.max;
            lines.push(`    float ${nodeName}_value = ${inputVal};`);
            lines.push(`    if (${nodeName}_value < ${numberLiteral(min, `Saturation '${node.id}' min`)}) ${nodeName}_value = ${numberLiteral(min, `Saturation '${node.id}' min`)};`);
            lines.push(`    if (${nodeName}_value > ${numberLiteral(max, `Saturation '${node.id}' max`)}) ${nodeName}_value = ${numberLiteral(max, `Saturation '${node.id}' max`)};`);

        } else if (node.type === 'Switch') {
            const condEdge = inbound.get(`${node.id}.condition`);
            const trueEdge = inbound.get(`${node.id}.true_value`);
            const falseEdge = inbound.get(`${node.id}.false_value`);
            if (!condEdge || !trueEdge || !falseEdge) fail(`Switch '${node.id}' requires 'condition', 'true_value', and 'false_value' input edges`);
            const condVal = `${cIdentifier(condEdge.node.id, 'node id')}_${condEdge.port}`;
            const trueVal = `${cIdentifier(trueEdge.node.id, 'node id')}_${trueEdge.port}`;
            const falseVal = `${cIdentifier(falseEdge.node.id, 'node id')}_${falseEdge.port}`;
            lines.push(`    float ${nodeName}_value = ${condVal} ? ${trueVal} : ${falseVal};`);

        } else if (node.type === 'ControlLoop') {
            // ControlLoop has inputs: setpoint, measurement, enable
            // Outputs: command, error
            
            const setpointEdge = inbound.get(`${node.id}.setpoint`);
            const measurementEdge = inbound.get(`${node.id}.measurement`);
            const enableEdge = inbound.get(`${node.id}.enable`);
            
            if (!setpointEdge) fail(`ControlLoop '${node.id}' requires a 'setpoint' input edge`);
            if (!measurementEdge) fail(`ControlLoop '${node.id}' requires a 'measurement' input edge`);
            // enable is optional
            
            const setpointVal = `${cIdentifier(setpointEdge.node.id, 'node id')}_${setpointEdge.port}`;
            const measurementVal = `${cIdentifier(measurementEdge.node.id, 'node id')}_${measurementEdge.port}`;
            
            // Generate PID state variable name
            const pidState = `${nodeName}_pid_state`;
            
            // Initialize PID state on first call
            lines.push(`    static hyp_pid_state_t ${pidState} = {`);
            lines.push(`        .kp = ${numberLiteral(node.params.kp, `ControlLoop '${node.id}' kp`)},`);
            lines.push(`        .ki = ${numberLiteral(node.params.ki, `ControlLoop '${node.id}' ki`)},`);
            lines.push(`        .kd = ${numberLiteral(node.params.kd, `ControlLoop '${node.id}' kd`)},`);
            lines.push(`        .sample_period_s = ${numberLiteral(node.params.samplePeriodUs / 1e6, `ControlLoop '${node.id}' samplePeriodUs`)},`);
            lines.push(`        .output_min = ${numberLiteral(node.params.outputMin, `ControlLoop '${node.id}' outputMin`)},`);
            lines.push(`        .output_max = ${numberLiteral(node.params.outputMax, `ControlLoop '${node.id}' outputMax`)},`);
            lines.push(`        .integral = 0.0f,`);
            lines.push(`        .prev_error = 0.0f,`);
            lines.push(`        .initialized = false`);
            lines.push(`    };`);
            
            // Initial output if provided
            if (node.params.initialOutput !== undefined && node.params.initialOutput !== 0.0) {
                lines.push(`    if (!${pidState}.initialized) {`);
                lines.push(`        ${pidState}.integral = ${numberLiteral(node.params.initialOutput, `ControlLoop '${node.id}' initialOutput`)} / ${pidState}.kp;`);
                lines.push(`    }`);
            }
            
            // Call hyp_pid_step
            const enableVal = enableEdge ? `${cIdentifier(enableEdge.node.id, 'node id')}_${enableEdge.port}` : 'true';
            lines.push(`    float ${nodeName}_command = 0.0f;`);
            lines.push(`    float ${nodeName}_error = 0.0f;`);
            lines.push(`    hyp_pid_step(&${pidState}, ${setpointVal}, ${measurementVal}, ${enableVal}, &${nodeName}_command, &${nodeName}_error);`);

        } else if (node.type === 'UARTInput') {
            // UARTInput reads available bytes from UART
            const resourceId = hardwareResource(node, 'uart');
            
            const valueUsed = usedOutputs.has(`${node.id}.value`);
            const validUsed = usedOutputs.has(`${node.id}.valid`);
            
            if (valueUsed) {
                lines.push(`    int ${nodeName}_value = 0;`);
            }
            if (validUsed) {
                lines.push(`    uint8_t ${nodeName}_valid = 0;`);
            }
            
            if (valueUsed) {
                lines.push(`    int ${nodeName}_result = hyp_sensor_read(${cString(resourceId)}, &${nodeName}_value, sizeof(${nodeName}_value));`);
                lines.push(`    if (${nodeName}_result == HYP_RUNTIME_OK) {`);
                if (validUsed) {
                    lines.push(`        ${nodeName}_valid = 1;`);
                }
                lines.push(`    }`);
            }

        } else if (node.type === 'UARTOutput') {
                    // UARTOutput writes bytes to UART
                    const dataEdge = inbound.get(`${node.id}.data`);
                    if (!dataEdge) fail(`UARTOutput '${node.id}' requires a 'data' input edge`);
                    const dataVal = `${cIdentifier(dataEdge.node.id, 'node id')}_${dataEdge.port}`;

                    const resourceId = hardwareResource(node, 'uart');

                    const sentUsed = usedOutputs.has(`${node.id}.sent`);
                    lines.push(`    uint8_t ${nodeName}_sent = 0;`);
                    lines.push(`    int ${nodeName}_result = hyp_actuator_write(${cString(resourceId)}, &${dataVal}, sizeof(${dataVal}));`);
                    lines.push(`    if (${nodeName}_result == HYP_RUNTIME_OK) {`);
                    lines.push(`        ${nodeName}_sent = 1;`);
                    lines.push(`    }`);
                    if (!sentUsed) lines.push(`    (void)${nodeName}_sent;`);

                } else if (node.type === 'EncoderInput') {
                    // EncoderInput reads position/velocity from encoder
                    const resourceId = hardwareResource(node, 'encoder');

                    const positionUsed = usedOutputs.has(`${node.id}.position`);
                    const velocityUsed = usedOutputs.has(`${node.id}.velocity`);
                    const validUsed = usedOutputs.has(`${node.id}.valid`);

                    if (positionUsed) {
                        lines.push(`    int32_t ${nodeName}_position = 0;`);
                    }
                    if (velocityUsed) {
                        lines.push(`    float ${nodeName}_velocity = 0.0f;`);
                    }
                    if (validUsed) {
                        lines.push(`    uint8_t ${nodeName}_valid = 0;`);
                    }

                    if (positionUsed || velocityUsed) {
                        lines.push(`    int ${nodeName}_result = hyp_encoder_read(${cString(resourceId)}, ${positionUsed ? `&${nodeName}_position` : 'NULL'}, ${velocityUsed ? `&${nodeName}_velocity` : 'NULL'});`);
                        lines.push(`    if (${nodeName}_result == HYP_RUNTIME_OK) {`);
                        if (validUsed) {
                            lines.push(`        ${nodeName}_valid = 1;`);
                        }
                        lines.push(`    }`);
                    }

                } else if (node.type === 'MotorOutput') {
                    // MotorOutput writes command to motor via PWM
                    const commandEdge = inbound.get(`${node.id}.command`);
                    if (!commandEdge) fail(`MotorOutput '${node.id}' requires a 'command' input edge`);
                    const commandVal = `${cIdentifier(commandEdge.node.id, 'node id')}_${commandEdge.port}`;

                    const resourceId = hardwareResource(node, 'motor');

                    const appliedUsed = usedOutputs.has(`${node.id}.applied`);
                    const activeUsed = usedOutputs.has(`${node.id}.active`);
                    const enableEdge = inbound.get(`${node.id}.enable`);
                    const enableVal = enableEdge ? `${cIdentifier(enableEdge.node.id, 'node id')}_${enableEdge.port}` : 'true';

                    const min = numberLiteral(node.params.min, `MotorOutput '${node.id}' min`);
                    const max = numberLiteral(node.params.max, `MotorOutput '${node.id}' max`);
                    if (node.params.max <= node.params.min) fail(`MotorOutput '${node.id}' max must be greater than min`);
                    const safe = numberLiteral(node.params.safeValue === undefined ? node.params.min : node.params.safeValue, `MotorOutput '${node.id}' safeValue`);

                    lines.push(`    float ${nodeName}_applied = ${commandVal};`);
                    lines.push(`    if (${nodeName}_applied < ${min}) ${nodeName}_applied = ${min};`);
                    lines.push(`    if (${nodeName}_applied > ${max}) ${nodeName}_applied = ${max};`);
                    lines.push(`    float ${nodeName}_duty = (${nodeName}_applied - ${min}) / (${max} - ${min});`);
                    lines.push(`    float ${nodeName}_safe_value = ${safe};`);
                    lines.push(`    if (${nodeName}_safe_value < ${min}) ${nodeName}_safe_value = ${min};`);
                    lines.push(`    if (${nodeName}_safe_value > ${max}) ${nodeName}_safe_value = ${max};`);
                    lines.push(`    uint8_t ${nodeName}_active = 0;`);
                    lines.push(`    if (${enableVal}) ${nodeName}_active = (hyp_actuator_write(${cString(resourceId)}, &${nodeName}_duty, sizeof(${nodeName}_duty)) == HYP_RUNTIME_OK);`);
                    lines.push(`    else { float ${nodeName}_safe_duty = (${nodeName}_safe_value - ${min}) / (${max} - ${min}); hyp_actuator_write(${cString(resourceId)}, &${nodeName}_safe_duty, sizeof(${nodeName}_safe_duty)); }`);
                    if (!appliedUsed) lines.push(`    (void)${nodeName}_applied;`);
                    if (!enableEdge || !activeUsed) lines.push(`    (void)${nodeName}_safe_value;`);
                    if (!activeUsed) lines.push(`    (void)${nodeName}_active;`);

                } else if (node.type === 'WheelSpeed') {
                    // WheelSpeed converts encoder count to linear speed (m/s)
                    const encoderEdge = inbound.get(`${node.id}.encoder_count`);
                    if (!encoderEdge) fail(`WheelSpeed '${node.id}' requires an 'encoder_count' input edge`);
                    const encoderVal = `${cIdentifier(encoderEdge.node.id, 'node id')}_${encoderEdge.port}`;

                    const speedUsed = usedOutputs.has(`${node.id}.speed`);
                    if (speedUsed) {
                        const ppr = numberLiteral(node.params.pulsesPerRevolution, `WheelSpeed '${node.id}' pulsesPerRevolution`);
                        const radius = numberLiteral(node.params.wheelRadiusMeters, `WheelSpeed '${node.id}' wheelRadiusMeters`);
                        lines.push(`    float ${nodeName}_speed = (${encoderVal} * 2.0f * 3.14159265358979323846f * ${radius}) / ${ppr};`);
                    } else {
                        lines.push(`    (void)${encoderVal};`);
                    }

                } else if (node.type === 'DifferentialDrive') {
                    // DifferentialDrive converts linear/angular velocity to left/right motor commands
                    const linearEdge = inbound.get(`${node.id}.linear_velocity`);
                    const angularEdge = inbound.get(`${node.id}.angular_velocity`);
                    if (!linearEdge) fail(`DifferentialDrive '${node.id}' requires a 'linear_velocity' input edge`);
                    if (!angularEdge) fail(`DifferentialDrive '${node.id}' requires an 'angular_velocity' input edge`);
                    const linearVal = `${cIdentifier(linearEdge.node.id, 'node id')}_${linearEdge.port}`;
                    const angularVal = `${cIdentifier(angularEdge.node.id, 'node id')}_${angularEdge.port}`;

                    const leftUsed = usedOutputs.has(`${node.id}.left_cmd`);
                    const rightUsed = usedOutputs.has(`${node.id}.right_cmd`);
                    const trackWidth = numberLiteral(node.params.trackWidthMeters, `DifferentialDrive '${node.id}' trackWidthMeters`);

                    if (leftUsed) {
                        lines.push(`    float ${nodeName}_left_cmd = ${linearVal} - (${angularVal} * ${trackWidth}) / 2.0f;`);
                    } else {
                        lines.push(`    (void)${linearVal};`);
                        lines.push(`    (void)${angularVal};`);
                    }
                    if (rightUsed) {
                        lines.push(`    float ${nodeName}_right_cmd = ${linearVal} + (${angularVal} * ${trackWidth}) / 2.0f;`);
                    }

                } else {
                    // For now, other node types are not yet implemented in codegen
                    fail(`node '${node.id}' type '${node.type}' codegen not yet implemented`);
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

if (require.main === module) main();

module.exports = { generate };
