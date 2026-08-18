/**
 * index.js — HyprAccel KiCad Schematic Import Foundation (SCH-T1..SCH-T4)
 *
 * Exposes unified API for parsing KiCad schematics and converting them into
 * HyprAccel's canonical hardware resource model.
 */

const { parseKiCadSchematic } = require('./kicad_parser');
const { extractNormalizedSchematic } = require('./net_extractor');
const { resolveTargetMcu } = require('./mcu_resolver');
const { generateHardwareResourceModel } = require('./resource_mapper');

/**
 * Full import pipeline: KiCad .kicad_sch text -> Hardware Resource Model
 * @param {string} schContent - Raw .kicad_sch content string
 * @param {Object} [options] - Import options
 * @returns {Object} Import results containing raw parsed, normalized model, resolution, and hardware model
 */
function importKiCadSchematic(schContent, options = {}) {
    // Phase 1: SCH-T1 — Parse S-expression
    const rawParsed = parseKiCadSchematic(schContent);

    // Phase 2: SCH-T2 — Extract normalized Symbol/Pin/Net graph
    const normalizedModel = extractNormalizedSchematic(rawParsed);

    // Phase 3: SCH-T3 — Resolve target MCU and board capabilities
    const targetResolution = resolveTargetMcu(normalizedModel, options.boardsDict);

    // If unresolved, return structured unresolved target status
    if (targetResolution.status !== 'resolved') {
        return {
            status: 'unresolved',
            reason: targetResolution.reason,
            candidateSymbols: targetResolution.candidateSymbols,
            rawParsed,
            normalizedModel,
            targetResolution
        };
    }

    // Phase 4: SCH-T4 — Generate canonical HyprAccel hardware resource model
    const resourceResult = generateHardwareResourceModel(normalizedModel, targetResolution);

    return {
        status: 'success',
        boardKey: resourceResult.boardKey,
        hardware: resourceResult.hardware,
        targetResolution,
        rawParsed,
        normalizedModel,
        extractedAssignmentsCount: resourceResult.extractedAssignmentsCount
    };
}

module.exports = {
    parseKiCadSchematic,
    extractNormalizedSchematic,
    resolveTargetMcu,
    generateHardwareResourceModel,
    importKiCadSchematic
};
