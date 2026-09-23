#!/usr/bin/env node
'use strict';

/**
 * check_pin_conflicts.js — thin CLI + exported function around the single
 * source of truth for pin-conflict checking, boards/codegen/pin_conflicts.js.
 * No rule logic lives here; this file only wires stdin JSON -> mergeProjectConfig
 * -> checkPinConflicts -> stdout JSON, so desktop (via QProcess) and any other
 * non-Node caller can reuse exactly what codegen would block on.
 *
 * Usage:
 *   node check_pin_conflicts.js [path/to/boards.yaml] < payload.json
 *
 * Stdin payload: { board, hardware, clock?, graphs? }
 *   - board:   board key into boards.yaml (informational; hardware.board is
 *              what is actually used to look up the board, same as codegen)
 *   - hardware: hardware.json shape ({ version, board, resources, assignments, devices })
 *   - clock:   optional clock.json shape (folded in via mergeProjectConfig)
 *   - graphs:  optional array of graph documents (default: [])
 *
 * Stdout on success (exit 0): { errors: [Issue], warnings: [Issue] }
 * Stdout on failure (exit 2): { error: string }
 */

const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');
const { mergeProjectConfig } = require('./project_config');
const { checkPinConflicts } = require('./pin_conflicts');

const DEFAULT_BOARDS_YAML = path.join(__dirname, '..', 'boards.yaml');

function readStdin() {
    try {
        return fs.readFileSync(0, 'utf8');
    } catch (err) {
        throw new Error(`Could not read stdin: ${err.message}`);
    }
}

/**
 * Pure-ish: takes the already-parsed payload and a boards.yaml path, returns
 * { errors, warnings }. Throws on malformed input or unknown board.
 */
function checkFromPayload(payload, boardsYamlPath) {
    if (!payload || typeof payload !== 'object' || Array.isArray(payload)) {
        throw new Error('Payload must be a JSON object.');
    }
    const hardware = payload.hardware;
    if (!hardware || typeof hardware !== 'object' || Array.isArray(hardware)) {
        throw new Error('Payload must include a `hardware` object (hardware.json shape).');
    }
    if (typeof hardware.board !== 'string' || !hardware.board) {
        throw new Error('Payload `hardware.board` must be a non-empty string.');
    }
    if (!Array.isArray(hardware.assignments)) {
        throw new Error('Payload `hardware.assignments` must be an array.');
    }

    let boardsDoc;
    try {
        boardsDoc = yaml.load(fs.readFileSync(boardsYamlPath, 'utf8'));
    } catch (err) {
        throw new Error(`Could not read boards.yaml at ${boardsYamlPath}: ${err.message}`);
    }
    const boards = (boardsDoc && boardsDoc.boards) || {};
    const boardKey = hardware.board;
    const boardData = Object.prototype.hasOwnProperty.call(boards, boardKey) ? boards[boardKey] : null;
    if (!boardData) {
        throw new Error(`Unknown board '${boardKey}' in ${boardsYamlPath}.`);
    }

    const clock = payload.clock != null ? payload.clock : null;
    const config = mergeProjectConfig(hardware, clock, boardData);
    const graphs = Array.isArray(payload.graphs) ? payload.graphs : [];
    return checkPinConflicts(config, boardData, graphs);
}

function main() {
    const boardsYamlPath = process.argv[2] ? path.resolve(process.argv[2]) : DEFAULT_BOARDS_YAML;
    let payload;
    try {
        const raw = readStdin();
        payload = JSON.parse(raw);
    } catch (err) {
        process.stdout.write(JSON.stringify({ error: `Invalid JSON on stdin: ${err.message}` }) + '\n');
        process.exit(2);
        return;
    }

    try {
        const result = checkFromPayload(payload, boardsYamlPath);
        process.stdout.write(JSON.stringify(result) + '\n');
        process.exit(0);
    } catch (err) {
        process.stdout.write(JSON.stringify({ error: err.message }) + '\n');
        process.exit(2);
    }
}

if (require.main === module) {
    main();
}

module.exports = { checkFromPayload };
