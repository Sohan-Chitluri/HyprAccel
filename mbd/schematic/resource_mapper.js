/**
 * resource_mapper.js — Hardware Resource Model Generator (SCH-T4)
 *
 * Maps extracted KiCad schematic pin connections into HyprAccel's canonical
 * hardware resource model (projectHardware structure).
 */

const { projectHardware } = require('../editor/server');

/**
 * Canonical semantic roles supported by HyprAccel hardware model
 */
const CANONICAL_ROLES = Object.freeze({
    spi: ['sck', 'mosi', 'miso', 'cs'],
    uart: ['tx', 'rx'],
    i2c: ['sda', 'scl'],
    pwm: ['output'],
    adc: ['input'],
    gpio: ['gpio']
});

/**
 * Infer peripheral role from pin name or connected net name.
 */
function inferSignalRole(text) {
    if (!text || typeof text !== 'string') return null;
    const t = text.toUpperCase();

    // SPI
    if (/\b(SCK|SCLK|SPI.*CLK|CLK)\b/.test(t) && !t.includes('I2C') && !t.includes('IIC')) return { type: 'spi', role: 'sck' };
    if (/\b(MOSI|SDO|SPI.*MOSI|DIN)\b/.test(t)) return { type: 'spi', role: 'mosi' };
    if (/\b(MISO|SDI|SPI.*MISO|DOUT)\b/.test(t)) return { type: 'spi', role: 'miso' };
    if (/\b(CS|SS|NSS|CSN|SPI.*CS)\b/.test(t)) return { type: 'spi', role: 'cs' };

    // UART
    if (/\b(TX|TXD|UART.*TX|SOUT)\b/.test(t)) return { type: 'uart', role: 'tx' };
    if (/\b(RX|RXD|UART.*RX|SIN)\b/.test(t)) return { type: 'uart', role: 'rx' };

    // I2C
    if (/\b(SDA|I2C.*SDA|IIC.*SDA)\b/.test(t)) return { type: 'i2c', role: 'sda' };
    if (/\b(SCL|I2C.*SCL|IIC.*SCL)\b/.test(t)) return { type: 'i2c', role: 'scl' };

    // PWM
    if (/\b(PWM|SERVO|LEDC)\w*\b/.test(t)) return { type: 'pwm', role: 'output' };

    // ADC
    // Avoid classifying application labels such as AIN1/AIN2 as ADC unless
    // the schematic/net explicitly says ADC/ANALOG.
    if (/\b(ADC|ANALOG)\w*\b/.test(t)) return { type: 'adc', role: 'input' };

    // GPIO fallback
    if (/\b(GPIO|IO|LED|BTN|SW|IN|OUT)\w*\b/.test(t)) return { type: 'gpio', role: 'gpio' };

    return null;
}

/**
 * Generate HyprAccel hardware resource model from schematic & target resolution.
 * @param {Object} normalizedModel - Output of extractNormalizedSchematic()
 * @param {Object} targetResolution - Output of resolveTargetMcu()
 * @returns {Object} Canonical HyprAccel hardware model or unresolved status
 */
function generateHardwareResourceModel(normalizedModel, targetResolution) {
    if (!targetResolution || targetResolution.status !== 'resolved') {
        return {
            status: 'unresolved',
            reason: targetResolution ? targetResolution.reason : 'Target MCU is unresolved.',
            candidateSymbols: targetResolution ? targetResolution.candidateSymbols : []
        };
    }

    const boardKey = targetResolution.boardKey;
    const boardDef = targetResolution.boardDef;
    const mcuRef = targetResolution.mcuSymbol.reference;
    const mcuSym = normalizedModel.symbolsMap.get(mcuRef);

    if (!mcuSym) {
        throw new Error(`MCU symbol '${mcuRef}' not found in normalized schematic model.`);
    }

    const rawAssignments = [];
    const usedPins = new Set();

    for (const pin of mcuSym.pins) {
        const physicalPin = targetResolution.pinMap[pin.number] || targetResolution.pinMap[pin.name] || pin.name;
        if (!physicalPin || usedPins.has(physicalPin)) continue;

        // Trace net and connected external pins
        const traces = normalizedModel.findTrace(mcuRef, pin.number);
        const netName = pin.nets.length > 0 ? pin.nets[0] : '';
        const extPinName = traces.length > 0 && traces[0].externalPin ? traces[0].externalPin.name : '';

        // Context text for role inference
        const contextText = `${pin.name} ${netName} ${extPinName}`;
        const inferred = inferSignalRole(contextText);

        if (!inferred) continue;

        // Match pin to board peripheral instance in boardDef.pins
        let matchedResource = null;

        if (inferred.type === 'spi' && boardDef.pins.spi) {
            for (const [inst, obj] of Object.entries(boardDef.pins.spi)) {
                if (obj[inferred.role] === physicalPin) {
                    matchedResource = `spi.${inst}`;
                    break;
                }
            }
        } else if (inferred.type === 'uart' && boardDef.pins.uart) {
            for (const [inst, obj] of Object.entries(boardDef.pins.uart)) {
                if (obj[inferred.role] === physicalPin) {
                    matchedResource = `uart.${inst}`;
                    break;
                }
            }
        } else if (inferred.type === 'i2c' && boardDef.pins.i2c) {
            for (const [inst, obj] of Object.entries(boardDef.pins.i2c)) {
                if (obj[inferred.role] === physicalPin) {
                    matchedResource = `i2c.${inst}`;
                    break;
                }
            }
        } else if (inferred.type === 'pwm' && Array.isArray(boardDef.pins.pwm) && boardDef.pins.pwm.includes(physicalPin)) {
            matchedResource = `pwm.${physicalPin}`;
        } else if (inferred.type === 'adc' && Array.isArray(boardDef.pins.adc) && boardDef.pins.adc.includes(physicalPin)) {
            matchedResource = `adc.${physicalPin}`;
        } else if (inferred.type === 'gpio' && Array.isArray(boardDef.pins.gpio) && boardDef.pins.gpio.includes(physicalPin)) {
            matchedResource = `gpio.${physicalPin}`;
        }

        if (matchedResource) {
            usedPins.add(physicalPin);
            rawAssignments.push({
                node: `Schematic:${mcuRef}:${pin.name || pin.number}`,
                role: inferred.role,
                pin: physicalPin,
                resource: matchedResource
            });
        }
    }

    // Pass through HyprAccel projectHardware validator for canonical structure
    const canonicalModel = projectHardware(boardKey, rawAssignments);

    return {
        status: 'resolved',
        boardKey,
        hardware: canonicalModel,
        extractedAssignmentsCount: rawAssignments.length
    };
}

module.exports = {
    CANONICAL_ROLES,
    inferSignalRole,
    generateHardwareResourceModel
};
