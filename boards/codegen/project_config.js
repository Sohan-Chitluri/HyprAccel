'use strict';
/*
 * project_config.js — the ONE project-config read/merge step for codegen.
 * CONTRACT (owned by the integrator). Used by mbd/editor/server.js and by
 * gen_board_config.js --project. Do not add another loader elsewhere.
 *
 * Merged config shape (what every codegen consumer receives):
 *   { ...hardware.json fields unchanged (version, board, resources, assignments, devices),
 *     clock: null | {
 *       board, selections: {nodeId: value}, resolved: {nodeId: MHz}, errors: [string],
 *       bestEffort: true,                 // always: values are not datasheet-verified
 *       unverifiedNodes: [nodeId],        // boards.yaml clocks nodes whose note says "unverified"
 *     },
 *     configWarnings: [string] }          // problems found while merging (never thrown)
 * The merged object is read-only input for codegen and MUST NEVER be written
 * back to hardware.json (writeHardwareConfig) — clock lives only in clock.json.
 */

const fs   = require('fs');
const path = require('path');
const yaml = require('js-yaml');

/** Reads <projectDir>/hardware/clock.json. null if absent; throws on invalid JSON/shape. */
function readClockConfig(projectDir) {
    const clockPath = path.join(projectDir, 'hardware', 'clock.json');
    let raw;
    try {
        raw = fs.readFileSync(clockPath, 'utf8');
    } catch (err) {
        if (err.code === 'ENOENT') return null;
        throw new Error(`Could not read clock config at ${clockPath}: ${err.message}`);
    }
    let parsed;
    try {
        parsed = JSON.parse(raw);
    } catch (err) {
        throw new Error(`Invalid JSON in clock config at ${clockPath}: ${err.message}`);
    }
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
        throw new Error(`Invalid clock config at ${clockPath}: expected an object.`);
    }
    if (parsed.version !== 1) {
        throw new Error(`Invalid clock config at ${clockPath}: unsupported version '${parsed.version}'.`);
    }
    if (!parsed.selections || typeof parsed.selections !== 'object' || Array.isArray(parsed.selections)) {
        throw new Error(`Invalid clock config at ${clockPath}: 'selections' must be an object.`);
    }
    const resolved = (parsed.resolved && typeof parsed.resolved === 'object' && !Array.isArray(parsed.resolved))
        ? parsed.resolved : {};
    const errors = Array.isArray(parsed.errors) ? parsed.errors : [];
    return {
        version: parsed.version,
        board: parsed.board,
        selections: parsed.selections,
        resolved,
        errors,
    };
}

/**
 * Pure: folds a clock.json object (or null) into a hardware object without
 * mutating either and without touching resources/assignments/devices/nodes.
 * boardData = parsed boards.yaml boards[hardware.board] (for clocks validation).
 */
function mergeProjectConfig(hardware, clock, boardData) {
    const hardwareCopy = JSON.parse(JSON.stringify(hardware));
    const configWarnings = [];

    if (!clock) {
        return { ...hardwareCopy, clock: null, configWarnings };
    }

    const clockCopy = JSON.parse(JSON.stringify(clock));

    if (clockCopy.board !== hardwareCopy.board) {
        configWarnings.push(
            `clock.json board '${clockCopy.board}' does not match hardware board '${hardwareCopy.board}'; ignoring clock config.`
        );
        return { ...hardwareCopy, clock: null, configWarnings };
    }

    if (!boardData || !boardData.clocks || !Array.isArray(boardData.clocks.nodes)) {
        configWarnings.push(`Board '${hardwareCopy.board}' has no clocks section in boards.yaml; ignoring clock config.`);
        return { ...hardwareCopy, clock: null, configWarnings };
    }

    const nodes = boardData.clocks.nodes;
    const nodeIds = new Set(nodes.map(n => n.id));

    const selections = {};
    for (const [key, value] of Object.entries(clockCopy.selections || {})) {
        if (!nodeIds.has(key)) {
            configWarnings.push(`clock.json selections has unknown node id '${key}'; dropped.`);
            continue;
        }
        selections[key] = value;
    }

    const resolved = {};
    for (const [key, value] of Object.entries(clockCopy.resolved || {})) {
        if (!nodeIds.has(key)) {
            configWarnings.push(`clock.json resolved has unknown node id '${key}'; dropped.`);
            continue;
        }
        if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) {
            configWarnings.push(`clock.json resolved value for '${key}' is not a finite non-negative number; dropped.`);
            continue;
        }
        resolved[key] = value;
    }

    const unverifiedNodes = nodes
        .filter(n => typeof n.note === 'string' && n.note.toLowerCase().includes('unverified'))
        .map(n => n.id);

    const mergedClock = {
        board: clockCopy.board,
        selections,
        resolved,
        errors: Array.isArray(clockCopy.errors) ? clockCopy.errors : [],
        bestEffort: true,
        unverifiedNodes,
    };

    return { ...hardwareCopy, clock: mergedClock, configWarnings };
}

/** Reads <projectDir>/hardware/hardware.json (raw, no migration) + clock.json and merges. */
function readProjectConfig(projectDir, boardsYamlPath) {
    const hardwarePath = path.join(projectDir, 'hardware', 'hardware.json');
    let hardwareRaw;
    try {
        hardwareRaw = fs.readFileSync(hardwarePath, 'utf8');
    } catch (err) {
        if (err.code === 'ENOENT') {
            throw new Error(`Project hardware configuration not found at ${hardwarePath}.`);
        }
        throw new Error(`Could not read project hardware configuration at ${hardwarePath}: ${err.message}`);
    }

    let hardware;
    try {
        hardware = JSON.parse(hardwareRaw);
    } catch (err) {
        throw new Error(`Invalid JSON in project hardware configuration at ${hardwarePath}: ${err.message}`);
    }

    const boardsDoc = yaml.load(fs.readFileSync(boardsYamlPath, 'utf8'));
    const boards = (boardsDoc && boardsDoc.boards) || {};
    const boardKey = hardware.board;
    const boardData = Object.prototype.hasOwnProperty.call(boards, boardKey) ? boards[boardKey] : null;
    if (!boardData) {
        throw new Error(`Unknown board '${boardKey}' in ${hardwarePath}.`);
    }

    const clock = readClockConfig(projectDir);

    return mergeProjectConfig(hardware, clock, boardData);
}

module.exports = { readClockConfig, mergeProjectConfig, readProjectConfig };
