/**
 * mcu_resolver.js — MCU and HyprAccel Target Resolver (SCH-T3)
 *
 * Identifies target MCU symbols from the schematic and maps them to known
 * HyprAccel board descriptors (esp32, thejas32) from boards.yaml.
 * Returns structured "unresolved" result if target cannot be confidently resolved.
 */

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '../../');
const BOARDS_YAML = path.join(REPO_ROOT, 'boards/boards.yaml');

/**
 * Minimal YAML parser for boards.yaml (mirrors server.js / gen_board_config.js)
 */
function parseSimpleYaml(content) {
    const lines = content.split('\n');
    const result = { boards: {} };
    let currentBoard = null;
    let currentCategory = null;
    let currentPinSection = null;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].split('#')[0].trimEnd();
        if (line.trim().length === 0) continue;

        const indent = line.search(/\S/);
        const trimmed = line.trim();

        if (indent === 0 && trimmed === 'boards:') continue;

        if (indent === 2 && trimmed.endsWith(':')) {
            currentBoard = trimmed.slice(0, -1);
            result.boards[currentBoard] = {
                accelerators: [],
                pins: { gpio: [], spi: {}, i2c: {}, uart: {}, pwm: [], adc: [] }
            };
            currentCategory = null;
            currentPinSection = null;
            continue;
        }

        if (indent === 4 && currentBoard) {
            if (trimmed === 'pins:') { currentCategory = 'pins'; continue; }
            if (trimmed === 'accelerators:') { currentCategory = 'accelerators'; currentPinSection = null; continue; }
            if (trimmed.endsWith(':') && currentCategory !== 'pins') {
                currentCategory = trimmed.slice(0, -1);
            } else if (trimmed.includes(':') && currentCategory !== 'accelerators' && currentCategory !== 'pins') {
                const [key, ...rest] = trimmed.split(':');
                let val = rest.join(':').trim();
                if (val.startsWith('"') && val.endsWith('"')) val = val.slice(1, -1);
                else if (!isNaN(Number(val))) val = Number(val);
                result.boards[currentBoard][key.trim()] = val;
            }
            continue;
        }

        if (indent === 6 && currentBoard) {
            if (currentCategory === 'accelerators' && trimmed.startsWith('- "')) {
                result.boards[currentBoard].accelerators.push(trimmed.slice(3, -1));
                continue;
            }
            if (currentCategory === 'pins') {
                if (trimmed.endsWith(':')) { currentPinSection = trimmed.slice(0, -1); continue; }
            }
        }

        if (indent === 8 && currentBoard && currentCategory === 'pins' && currentPinSection) {
            const sec = currentPinSection;
            if (trimmed.startsWith('- "')) {
                const val = trimmed.slice(3, -1);
                if (['gpio', 'pwm', 'adc'].includes(sec)) {
                    result.boards[currentBoard].pins[sec].push(val);
                }
            } else if (trimmed.includes(': {')) {
                const [key, valStr] = trimmed.split(': {');
                const cleanValStr = valStr.replace('}', '').trim();
                const pairs = cleanValStr.split(',').map(s => s.trim());
                const obj = {};
                for (const pair of pairs) {
                    let [k, v] = pair.split(':');
                    if (!k || !v) continue;
                    k = k.trim(); v = v.trim();
                    if (v.startsWith('"') && v.endsWith('"')) v = v.slice(1, -1);
                    obj[k] = v;
                }
                result.boards[currentBoard].pins[sec][key.trim()] = obj;
            }
        }
    }
    return result;
}

/**
 * Load known boards definitions from boards.yaml.
 */
function loadBoardDefinitions(customYamlPath) {
    const yamlPath = customYamlPath || BOARDS_YAML;
    if (!fs.existsSync(yamlPath)) {
        throw new Error(`boards.yaml file not found at '${yamlPath}'.`);
    }
    const content = fs.readFileSync(yamlPath, 'utf8');
    return parseSimpleYaml(content).boards;
}

/**
 * Target matching patterns for supported HyprAccel boards.
 */
const BOARD_MATCH_PATTERNS = Object.freeze({
    esp32: [
        /\bESP32\b/i,
        /\bESP32-WROOM\b/i,
        /\bESP32-DEVKIT\b/i,
        /\bESP-WROOM-32\b/i,
        /\bESP32DEV\b/i,
        /\bESP32-S3\b/i,
        /\bESP32-C3\b/i
    ],
    thejas32: [
        /\bTHEJAS\b/i,
        /\bTHEJAS32\b/i,
        /\bARIES\b/i,
        /\bARIES_V2\b/i,
        /\bARIES-V2\b/i,
        /\bCDAC_THEJAS\b/i
    ]
});

/**
 * Resolves the target MCU from normalized schematic model.
 * @param {Object} normalizedModel - Output of extractNormalizedSchematic()
 * @param {Object} [boardsDict] - Optional pre-loaded boards dictionary from boards.yaml
 * @returns {Object} Resolution result object
 */
function resolveTargetMcu(normalizedModel, boardsDict) {
    if (!normalizedModel || !Array.isArray(normalizedModel.symbols)) {
        throw new Error('Invalid input: normalized schematic model with symbols array required.');
    }

    const boards = boardsDict || loadBoardDefinitions();
    const candidateMatches = [];

    for (const sym of normalizedModel.symbols) {
        // Skip obvious non-MCU symbols (resistors, capacitors, connectors, grounds, power)
        const refUpper = (sym.reference || '').toUpperCase();
        if (/^(R|C|L|D|J|JP|TP|FB|GND|\+3V3|\+5V|PWR)/.test(refUpper) && !refUpper.startsWith('U')) {
            continue;
        }

        const searchText = `${sym.reference} ${sym.value} ${sym.libId} ${JSON.stringify(sym.properties)}`;

        for (const [boardKey, patterns] of Object.entries(BOARD_MATCH_PATTERNS)) {
            const isMatch = patterns.some(pattern => pattern.test(searchText));
            if (isMatch) {
                candidateMatches.push({
                    boardKey,
                    symbol: sym,
                    boardDef: boards[boardKey] || null
                });
            }
        }
    }

    // 1. No matches found
    if (candidateMatches.length === 0) {
        const ICs = normalizedModel.symbols
            .filter(s => (s.reference || '').startsWith('U'))
            .map(s => ({ reference: s.reference, value: s.value, libId: s.libId }));

        return {
            status: 'unresolved',
            reason: 'No known HyprAccel target MCU (ESP32 / THEJAS32) found in schematic symbols.',
            candidateSymbols: ICs
        };
    }

    // 2. Multiple conflicting matches
    const matchedBoardKeys = new Set(candidateMatches.map(m => m.boardKey));
    if (matchedBoardKeys.size > 1) {
        return {
            status: 'unresolved',
            reason: `Multiple target MCUs detected in schematic: ${Array.from(matchedBoardKeys).join(', ')}.`,
            candidateSymbols: candidateMatches.map(m => ({
                boardKey: m.boardKey,
                reference: m.symbol.reference,
                value: m.symbol.value,
                libId: m.symbol.libId
            }))
        };
    }

    // 3. Single candidate board resolved
    const bestMatch = candidateMatches[0];
    const boardKey = bestMatch.boardKey;
    const mcuSymbol = bestMatch.symbol;
    const boardDef = bestMatch.boardDef;

    // Build physical pin mapping (Schematic pin number/name -> Physical board pin name)
    const pinMap = {};
    for (const pin of mcuSymbol.pins) {
        let physicalPin = null;

        // Check exact match in GPIO or peripheral pin lists
        const pName = (pin.name || '').toUpperCase();
        const pNum = (pin.number || '').toUpperCase();

        // ESP32 physical pin format (GPIOxx)
        if (/^GPIO\d+$/.test(pName)) {
            physicalPin = pName;
        } else if (/^IO\d+$/.test(pName)) {
            physicalPin = `GPIO${pName.slice(2)}`;
        } else if (/^\d+$/.test(pNum) && boardKey === 'esp32') {
            // Check if pin name or number corresponds to a GPIO
            const gpioName = `GPIO${pNum}`;
            if (boardDef && boardDef.pins && Array.isArray(boardDef.pins.gpio) && boardDef.pins.gpio.includes(gpioName)) {
                physicalPin = gpioName;
            }
        }

        // If no direct GPIOxx string match, check board.pins lists for matching pin names
        if (!physicalPin && boardDef && boardDef.pins) {
            if (Array.isArray(boardDef.pins.gpio)) {
                const matchedGpio = boardDef.pins.gpio.find(g => g.toUpperCase() === pName || g.toUpperCase() === `GPIO${pNum}`);
                if (matchedGpio) physicalPin = matchedGpio;
            }
        }

        // Fallback: use pin.name if non-empty, otherwise GPIO<pin.number>
        if (!physicalPin) {
            physicalPin = pin.name || `GPIO${pin.number}`;
        }

        pinMap[pin.number] = physicalPin;
        if (pin.name && pin.name !== pin.number) {
            pinMap[pin.name] = physicalPin;
        }
    }

    return {
        status: 'resolved',
        boardKey,
        boardDef,
        mcuSymbol: {
            uuid: mcuSymbol.uuid,
            reference: mcuSymbol.reference,
            value: mcuSymbol.value,
            libId: mcuSymbol.libId,
            pinsCount: mcuSymbol.pins.length
        },
        pinMap
    };
}

module.exports = {
    parseSimpleYaml,
    loadBoardDefinitions,
    resolveTargetMcu
};
