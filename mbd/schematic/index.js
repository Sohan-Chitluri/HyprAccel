/**
 * index.js — HyprAccel Schematic Import Foundation (SCH-T1..SCH-T8)
 *
 * Exposes unified API for parsing KiCad schematics (.kicad_sch) and EasyEDA
 * netlists (.tel) and converting them into HyprAccel's canonical hardware
 * resource model.
 */

const { parseKiCadSchematic } = require('./kicad_parser');
const { extractNormalizedSchematic } = require('./net_extractor');
const { resolveTargetMcu } = require('./mcu_resolver');
const { generateHardwareResourceModel } = require('./resource_mapper');
const { importEasyEDASchematic } = require('./easyeda_parser');

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

/**
 * Full import pipeline: EasyEDA .tel netlist text -> Hardware Resource Model
 * @param {string} telContent - Raw .tel netlist content string
 * @param {Object} [options] - Import options
 * @returns {Object} Import results containing raw parsed, normalized model, resolution, and hardware model
 */
function importEasyEDA(telContent, options = {}) {
    // Phase 1: SCH-T8 — Parse EasyEDA .tel netlist
    const { rawParsed, normalizedModel } = importEasyEDASchematic(telContent);

    // Phase 2: SCH-T3 — Resolve target MCU and board capabilities
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

    // Phase 3: SCH-T4 — Generate canonical HyprAccel hardware resource model
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

/**
 * Unified schematic import API - dispatches based on file extension
 * @param {string} filePath - Path to schematic file (.kicad_sch or .tel)
 * @param {string} content - File content
 * @param {Object} [options] - Import options
 * @returns {Object} Import results
 */
function importSchematic(filePath, content, options = {}) {
    const ext = path.extname(filePath).toLowerCase();

    if (ext === '.kicad_sch') {
        return importKiCadSchematic(content, options);
    } else if (ext === '.tel') {
        return importEasyEDA(content, options);
    } else {
        throw new Error(`Unsupported schematic file format: ${ext}. Supported: .kicad_sch, .tel`);
    }
}

const path = require('path');

module.exports = {
    parseKiCadSchematic,
    extractNormalizedSchematic,
    resolveTargetMcu,
    generateHardwareResourceModel,
    importKiCadSchematic,
    importEasyEDASchematic,
    importEasyEDA,
    importSchematic
};
