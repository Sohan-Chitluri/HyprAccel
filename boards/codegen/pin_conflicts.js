'use strict';
/*
 * pin_conflicts.js — pre-codegen pin validation. CONTRACT (owned by the integrator).
 * Pure function; uses only boards.yaml board data, the hardware config and graphs.
 */

const PIN_SCOPED_TYPES = ['gpio', 'pwm', 'adc'];
const BUS_TYPES = ['spi', 'i2c', 'uart'];

/* Build the canonical resource map for a board straight from boards.yaml
 * data (never from hardware.resources, which is only a UI convenience copy
 * and may be stale). Mirrors mbd/editor/server.js#defaultResourceConfig,
 * but keeps just what this module needs: for each resource id, its type,
 * instance, and a role -> pin map. */
function buildResources(boardData) {
    const pins = (boardData && boardData.pins) || {};
    const resources = {};

    for (const type of BUS_TYPES) {
        const instances = pins[type] || {};
        for (const [instance, signals] of Object.entries(instances)) {
            const roles = {};
            for (const [role, pin] of Object.entries(signals || {})) {
                /* i2c instances may carry a nested `devices` map of named
                 * slave addresses (SDK-T6) — that is not a pin role. */
                if (role === 'devices') continue;
                roles[role] = pin;
            }
            resources[`${type}.${instance}`] = { id: `${type}.${instance}`, type, instance, roles };
        }
    }

    for (const type of ['pwm', 'adc']) {
        const role = type === 'pwm' ? 'output' : 'input';
        for (const pin of pins[type] || []) {
            resources[`${type}.${pin}`] = { id: `${type}.${pin}`, type, instance: pin, roles: { [role]: pin } };
        }
    }

    for (const pin of pins.gpio || []) {
        resources[`gpio.${pin}`] = { id: `gpio.${pin}`, type: 'gpio', instance: pin, roles: { gpio: pin } };
    }

    for (const accelerator of boardData.accelerators || []) {
        const name = String(accelerator).toLowerCase();
        resources[`accelerator.${name}`] = { id: `accelerator.${name}`, type: 'accelerator', instance: name, roles: {} };
    }

    return resources;
}

function describeHwAssignment(resourceId, role) {
    const type = resourceId.split('.')[0];
    if (BUS_TYPES.includes(type)) {
        const instance = resourceId.slice(type.length + 1);
        return `${instance.toUpperCase()} ${String(role).toUpperCase()}`;
    }
    if (PIN_SCOPED_TYPES.includes(type)) return type.toUpperCase();
    return resourceId;
}

function describePinScopedType(type) {
    return type.toUpperCase();
}

function busLabel(resourceId) {
    const type = resourceId.split('.')[0];
    const instance = resourceId.slice(type.length + 1);
    return instance.toUpperCase();
}

function sortIssues(issues) {
    return issues.slice().sort((a, b) => {
        if (a.code !== b.code) return a.code < b.code ? -1 : 1;
        const ak = a.pin || a.resource || '';
        const bk = b.pin || b.resource || '';
        if (ak !== bk) return ak < bk ? -1 : 1;
        return 0;
    });
}

/**
 * @param hardware  hardware config (merged config is fine; clock is ignored)
 * @param boardData parsed boards.yaml boards[hardware.board]
 * @param graphs    array of graph documents to check against (may be empty)
 * @returns { errors: [Issue], warnings: [Issue] }
 *   Issue = { code: string, message: string, pin?: string, resource?: string, node?: string }
 *   errors block codegen; warnings are reported and emitted as header comments.
 */
function checkPinConflicts(hardware, boardData, graphs = []) {
    const errors = [];
    const warnings = [];

    const resources = buildResources(boardData || {});
    const assignments = (hardware && hardware.assignments) || [];
    graphs = graphs || [];

    /* ---- Step 1: validate each assignment (INVALID_ASSIGNMENT) --------- */
    const valid = []; // { pin, role, resource, node }
    for (const a of assignments) {
        if (!a || typeof a.pin !== 'string' || typeof a.role !== 'string' || typeof a.resource !== 'string') {
            errors.push({
                code: 'INVALID_ASSIGNMENT',
                pin: a && a.pin,
                resource: a && a.resource,
                message: `Assignment ${JSON.stringify(a)} is missing a pin, role, or resource.`
            });
            continue;
        }
        const resource = resources[a.resource];
        if (!resource) {
            errors.push({
                code: 'INVALID_ASSIGNMENT',
                pin: a.pin,
                resource: a.resource,
                message: `Assignment references unknown hardware resource '${a.resource}'.`
            });
            continue;
        }
        /* Accelerator resources never take pin assignments and never
         * conflict — skip them entirely rather than flag them. */
        if (resource.type === 'accelerator') continue;

        if (!Object.prototype.hasOwnProperty.call(resource.roles, a.role)) {
            errors.push({
                code: 'INVALID_ASSIGNMENT',
                pin: a.pin,
                resource: a.resource,
                message: `'${a.role}' is not a valid signal for hardware resource '${a.resource}'.`
            });
            continue;
        }
        const expectedPin = resource.roles[a.role];
        if (expectedPin !== a.pin) {
            errors.push({
                code: 'INVALID_ASSIGNMENT',
                pin: a.pin,
                resource: a.resource,
                message: `Pin ${a.pin} is not valid for signal '${a.role}' on hardware resource '${a.resource}' (expected ${expectedPin}).`
            });
            continue;
        }
        valid.push({ pin: a.pin, role: a.role, resource: a.resource, node: a.node });
    }

    /* ---- Step 2: PIN_MULTIPLE_FUNCTIONS --------------------------------- */
    const byPin = new Map();
    for (const entry of valid) {
        if (!byPin.has(entry.pin)) byPin.set(entry.pin, []);
        byPin.get(entry.pin).push(entry);
    }
    for (const [pin, entries] of byPin) {
        const combos = new Map(); // "resource|role" -> entry
        for (const entry of entries) combos.set(`${entry.resource}|${entry.role}`, entry);
        if (combos.size > 1) {
            const parts = [...combos.values()].map(e => `${e.resource} (${e.role})`);
            errors.push({
                code: 'PIN_MULTIPLE_FUNCTIONS',
                pin,
                message: `Pin ${pin} is assigned multiple functions: ${parts.join(', ')}.`
            });
        }
    }

    /* ---- Step 3: BUS_INCOMPLETE / BUS_PARTIAL --------------------------- */
    const byResource = new Map();
    for (const entry of valid) {
        if (!byResource.has(entry.resource)) byResource.set(entry.resource, []);
        byResource.get(entry.resource).push(entry);
    }
    for (const [id, resource] of Object.entries(resources)) {
        if (!BUS_TYPES.includes(resource.type)) continue;
        const entries = byResource.get(id) || [];
        if (entries.length === 0) continue; // nothing assigned -> no rule applies

        const assignedRoles = new Set(entries.map(e => e.role));
        const label = busLabel(id);

        if (resource.type === 'i2c') {
            const missing = ['sda', 'scl'].filter(r => !assignedRoles.has(r));
            if (missing.length > 0) {
                errors.push({
                    code: 'BUS_INCOMPLETE',
                    resource: id,
                    message: `${label} is missing ${missing.map(r => r.toUpperCase()).join(', ')}.`
                });
            }
        } else if (resource.type === 'spi') {
            const missingCore = ['sck', 'mosi'].filter(r => !assignedRoles.has(r));
            if (missingCore.length > 0) {
                errors.push({
                    code: 'BUS_INCOMPLETE',
                    resource: id,
                    message: `${label} is missing ${missingCore.map(r => r.toUpperCase()).join(', ')}.`
                });
            } else {
                if (!assignedRoles.has('miso')) {
                    warnings.push({
                        code: 'BUS_PARTIAL',
                        resource: id,
                        message: `${label} has no MISO assigned (write-only device).`
                    });
                }
                if (!assignedRoles.has('cs')) {
                    warnings.push({
                        code: 'BUS_PARTIAL',
                        resource: id,
                        message: `${label} has no CS assigned.`
                    });
                }
            }
        } else if (resource.type === 'uart') {
            const hasTx = assignedRoles.has('tx');
            const hasRx = assignedRoles.has('rx');
            if (hasTx && !hasRx) {
                warnings.push({
                    code: 'BUS_PARTIAL',
                    resource: id,
                    message: `${label} TX assigned without RX.`
                });
            } else if (hasRx && !hasTx) {
                warnings.push({
                    code: 'BUS_PARTIAL',
                    resource: id,
                    message: `${label} RX assigned without TX.`
                });
            }
        }
    }

    /* ---- Step 4: graph-level checks ------------------------------------- */
    const hwResourcesForPin = new Map(); // pin -> Set(resourceId)
    for (const entry of valid) {
        if (!hwResourcesForPin.has(entry.pin)) hwResourcesForPin.set(entry.pin, new Set());
        hwResourcesForPin.get(entry.pin).add(entry.resource);
    }

    const graphPinEntries = []; // { pin, resourceId, type, nodeId }
    for (const graph of graphs) {
        for (const node of (graph && graph.nodes) || []) {
            const resourceId = node && node.params && node.params.hardwareResource;
            if (!resourceId || typeof resourceId !== 'string') continue;
            const dot = resourceId.indexOf('.');
            if (dot < 0) continue;
            const type = resourceId.slice(0, dot);
            const rest = resourceId.slice(dot + 1);

            if (type === 'accelerator') continue; // never conflicts

            if (PIN_SCOPED_TYPES.includes(type)) {
                graphPinEntries.push({ pin: rest, resourceId, type, nodeId: node.id });
            } else if (BUS_TYPES.includes(type)) {
                const hasAssignment = valid.some(a => a.resource === resourceId);
                if (!hasAssignment) {
                    warnings.push({
                        code: 'GRAPH_RESOURCE_UNASSIGNED',
                        resource: resourceId,
                        node: node.id,
                        message: `Graph node '${node.id}' references hardware resource '${resourceId}', which has no pin assignments in Hardware Setup.`
                    });
                }
            }
            // Unrecognized resource id prefixes are ignored; not covered by
            // this module's contract.
        }
    }

    const graphByPin = new Map();
    for (const entry of graphPinEntries) {
        if (!graphByPin.has(entry.pin)) graphByPin.set(entry.pin, []);
        graphByPin.get(entry.pin).push(entry);
    }

    for (const [pin, entries] of graphByPin) {
        const hwSet = hwResourcesForPin.get(pin) || new Set();

        // Graph node vs. a hardware assignment that claims the same pin
        // for a different resource.
        for (const entry of entries) {
            for (const hwResource of hwSet) {
                if (hwResource === entry.resourceId) continue;
                const hwEntry = valid.find(a => a.resource === hwResource && a.pin === pin);
                const hwDescription = hwEntry ? describeHwAssignment(hwResource, hwEntry.role) : hwResource;
                errors.push({
                    code: 'GRAPH_PIN_CONFLICT',
                    pin,
                    resource: entry.resourceId,
                    node: entry.nodeId,
                    message: `${pin} is used as ${hwDescription} but graph node '${entry.nodeId}' reads it as ${describePinScopedType(entry.type)}.`
                });
            }
        }

        // Two graph nodes claiming the same pin via different pin-scoped
        // resources.
        const distinct = [...new Set(entries.map(e => e.resourceId))];
        if (distinct.length > 1) {
            const first = entries[0];
            for (const entry of entries.slice(1)) {
                if (entry.resourceId === first.resourceId) continue;
                errors.push({
                    code: 'GRAPH_PIN_CONFLICT',
                    pin,
                    resource: entry.resourceId,
                    node: entry.nodeId,
                    message: `${pin} is used as ${describePinScopedType(first.type)} by graph node '${first.nodeId}' and as ${describePinScopedType(entry.type)} by graph node '${entry.nodeId}'.`
                });
            }
        }
    }

    return { errors: sortIssues(errors), warnings: sortIssues(warnings) };
}

module.exports = { checkPinConflicts };
