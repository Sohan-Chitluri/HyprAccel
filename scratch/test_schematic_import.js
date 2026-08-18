/**
 * test_schematic_import.js — HyprAccel KiCad Schematic Import Test Suite
 *
 * Validates:
 * 1. valid .kicad_sch parsing
 * 2. symbol extraction
 * 3. pin extraction
 * 4. net extraction
 * 5. MCU identification
 * 6. known board/resource resolution
 * 7. unresolved target handling
 * 8. semantic SPI mapping
 * 9. semantic UART mapping
 * 10. semantic I2C mapping
 * 11. GPIO mapping
 * 12. malformed schematic handling
 */

const {
    parseKiCadSchematic,
    extractNormalizedSchematic,
    resolveTargetMcu,
    generateHardwareResourceModel,
    importKiCadSchematic
} = require('../mbd/schematic');

console.log('=== HYPRACCEL SCHEMATIC INTEGRATION TEST SUITE ===\n');

let passedTests = 0;
let totalTests = 0;

function assert(condition, message) {
    totalTests++;
    if (condition) {
        console.log(`[PASS] Test ${totalTests}: ${message}`);
        passedTests++;
    } else {
        console.error(`[FAIL] Test ${totalTests}: ${message}`);
        process.exitCode = 1;
    }
}

// -----------------------------------------------------------------------------
// Sample Fixture Schematics
// -----------------------------------------------------------------------------

const VALID_ESP32_SCH = `
(kicad_sch (version 20211123) (generator eeschema) (uuid "sch-001")
  (title_block (title "ESP32 Test Board") (company "HyprAccel"))
  (lib_symbols
    (symbol "ESP32-WROOM-32:ESP32-WROOM-32"
      (pin bidirectional line (at 0 0) (name "GPIO14" (effects (font (size 1.27 1.27)))) (number "13" (effects (font (size 1.27 1.27)))))
      (pin bidirectional line (at 0 10) (name "GPIO13" (effects (font (size 1.27 1.27)))) (number "16" (effects (font (size 1.27 1.27)))))
      (pin bidirectional line (at 0 20) (name "GPIO12" (effects (font (size 1.27 1.27)))) (number "14" (effects (font (size 1.27 1.27)))))
      (pin bidirectional line (at 0 30) (name "GPIO15" (effects (font (size 1.27 1.27)))) (number "23" (effects (font (size 1.27 1.27)))))
      (pin bidirectional line (at 0 40) (name "GPIO1" (effects (font (size 1.27 1.27)))) (number "34" (effects (font (size 1.27 1.27)))))
      (pin bidirectional line (at 0 50) (name "GPIO3" (effects (font (size 1.27 1.27)))) (number "35" (effects (font (size 1.27 1.27)))))
      (pin bidirectional line (at 0 60) (name "GPIO21" (effects (font (size 1.27 1.27)))) (number "33" (effects (font (size 1.27 1.27)))))
      (pin bidirectional line (at 0 70) (name "GPIO22" (effects (font (size 1.27 1.27)))) (number "36" (effects (font (size 1.27 1.27)))))
      (pin bidirectional line (at 0 80) (name "GPIO2" (effects (font (size 1.27 1.27)))) (number "24" (effects (font (size 1.27 1.27)))))
    )
    (symbol "Sensor:MPU6050"
      (pin bidirectional line (at 0 0) (name "SDA" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))
      (pin input line (at 0 10) (name "SCL" (effects (font (size 1.27 1.27)))) (number "2" (effects (font (size 1.27 1.27)))))
    )
  )
  (symbol (lib_id "ESP32-WROOM-32:ESP32-WROOM-32") (at 100 100 0) (uuid "u1-uuid")
    (property "Reference" "U1" (at 100 90 0))
    (property "Value" "ESP32-WROOM-32" (at 100 80 0))
    (pin "13" (uuid "p13-uuid") (at 0 0 0))
    (pin "16" (uuid "p16-uuid") (at 0 10 0))
    (pin "14" (uuid "p14-uuid") (at 0 20 0))
    (pin "23" (uuid "p23-uuid") (at 0 30 0))
    (pin "34" (uuid "p34-uuid") (at 0 40 0))
    (pin "35" (uuid "p35-uuid") (at 0 50 0))
    (pin "33" (uuid "p33-uuid") (at 0 60 0))
    (pin "36" (uuid "p36-uuid") (at 0 70 0))
    (pin "24" (uuid "p24-uuid") (at 0 80 0))
  )
  (symbol (lib_id "Sensor:MPU6050") (at 200 160 0) (uuid "u2-uuid")
    (property "Reference" "U2" (at 200 150 0))
    (property "Value" "MPU6050" (at 200 140 0))
    (pin "1" (uuid "u2p1-uuid") (at 0 0 0))
    (pin "2" (uuid "u2p2-uuid") (at 0 10 0))
  )
  (global_label "SPI_SCK" (shape input) (at 100 100 0) (uuid "l1-uuid"))
  (global_label "SPI_MOSI" (shape input) (at 100 110 0) (uuid "l2-uuid"))
  (global_label "SPI_MISO" (shape output) (at 100 120 0) (uuid "l3-uuid"))
  (global_label "SPI_CS" (shape input) (at 100 130 0) (uuid "l4-uuid"))
  (global_label "UART_TX" (shape output) (at 100 140 0) (uuid "l5-uuid"))
  (global_label "UART_RX" (shape input) (at 100 150 0) (uuid "l6-uuid"))
  (global_label "I2C_SDA" (shape bidirectional) (at 100 160 0) (uuid "l7-uuid"))
  (global_label "I2C_SCL" (shape input) (at 100 170 0) (uuid "l8-uuid"))
  (global_label "GPIO2" (shape output) (at 100 180 0) (uuid "l9-uuid"))
  (wire (pts (xy 100 100) (xy 150 100)) (uuid "w1-uuid"))
  (wire (pts (xy 100 110) (xy 150 110)) (uuid "w2-uuid"))
  (wire (pts (xy 100 120) (xy 150 120)) (uuid "w3-uuid"))
  (wire (pts (xy 100 130) (xy 150 130)) (uuid "w4-uuid"))
  (wire (pts (xy 100 140) (xy 150 140)) (uuid "w5-uuid"))
  (wire (pts (xy 100 150) (xy 150 150)) (uuid "w6-uuid"))
  (wire (pts (xy 100 160) (xy 150 160)) (uuid "w7-uuid"))
  (wire (pts (xy 100 170) (xy 150 170)) (uuid "w8-uuid"))
  (wire (pts (xy 100 180) (xy 150 180)) (uuid "w9-uuid"))
)
`;

const UNKNOWN_MCU_SCH = `
(kicad_sch (version 20211123) (generator eeschema) (uuid "sch-002")
  (symbol (lib_id "MCU_ST_STM32F4:STM32F407VGT6") (at 100 100 0) (uuid "u1-uuid")
    (property "Reference" "U1" (at 100 90 0))
    (property "Value" "STM32F407VGT6" (at 100 80 0))
    (pin "1" (uuid "p1-uuid"))
  )
)
`;

const MALFORMED_SCH = `
(kicad_sch (version 20211123
  (symbol (lib_id "ESP32-WROOM-32")
    (property "Reference" "U1"
`;

// -----------------------------------------------------------------------------
// Test Execution
// -----------------------------------------------------------------------------

// 1. Valid .kicad_sch parsing
try {
    const raw = parseKiCadSchematic(VALID_ESP32_SCH);
    assert(raw && raw.metadata.version === '20211123' && raw.metadata.titleBlock.title === 'ESP32 Test Board',
        'Valid .kicad_sch file parsed cleanly into AST with metadata.');
} catch (e) {
    assert(false, `Valid schematic parsing failed: ${e.message}`);
}

// 2. Symbol extraction
try {
    const raw = parseKiCadSchematic(VALID_ESP32_SCH);
    const norm = extractNormalizedSchematic(raw);
    assert(norm.symbols.length === 2 && norm.symbolsMap.has('U1') && norm.symbolsMap.has('U2'),
        'Extracted symbols U1 (ESP32-WROOM-32) and U2 (MPU6050) successfully.');
} catch (e) {
    assert(false, `Symbol extraction failed: ${e.message}`);
}

// 3. Pin extraction
try {
    const raw = parseKiCadSchematic(VALID_ESP32_SCH);
    const norm = extractNormalizedSchematic(raw);
    const u1 = norm.symbolsMap.get('U1');
    const gpio14Pin = u1.pins.find(p => p.name === 'GPIO14');
    assert(u1 && u1.pins.length === 9 && gpio14Pin && gpio14Pin.number === '13',
        'Extracted pins from MCU symbol with numbers and names.');
} catch (e) {
    assert(false, `Pin extraction failed: ${e.message}`);
}

// 4. Net extraction & MCU pin -> Net trace
try {
    const raw = parseKiCadSchematic(VALID_ESP32_SCH);
    const norm = extractNormalizedSchematic(raw);
    const spiSckNet = norm.netsMap.get('SPI_SCK');
    assert(spiSckNet && spiSckNet.members.some(m => m.symbolRef === 'U1' && m.pinNumber === '13'),
        'Extracted net SPI_SCK and mapped member MCU pin U1:13.');
} catch (e) {
    assert(false, `Net extraction failed: ${e.message}`);
}

// 5. MCU identification
try {
    const raw = parseKiCadSchematic(VALID_ESP32_SCH);
    const norm = extractNormalizedSchematic(raw);
    const res = resolveTargetMcu(norm);
    assert(res.status === 'resolved' && res.boardKey === 'esp32' && res.mcuSymbol.reference === 'U1',
        'Identified target MCU as ESP32 (symbol U1).');
} catch (e) {
    assert(false, `MCU identification failed: ${e.message}`);
}

// 6. Known board/resource resolution
try {
    const imported = importKiCadSchematic(VALID_ESP32_SCH);
    assert(imported.status === 'success' && imported.boardKey === 'esp32' && imported.hardware,
        'Resolved board and generated valid HyprAccel hardware model.');
} catch (e) {
    assert(false, `Board/resource resolution failed: ${e.message}`);
}

// 7. Unresolved target handling
try {
    const imported = importKiCadSchematic(UNKNOWN_MCU_SCH);
    assert(imported.status === 'unresolved' && imported.reason.includes('No known HyprAccel target MCU'),
        'Handled unknown target MCU (STM32F407VGT6) by returning structured unresolved target result.');
} catch (e) {
    assert(false, `Unresolved target handling failed: ${e.message}`);
}

// 8. Semantic SPI mapping
try {
    const imported = importKiCadSchematic(VALID_ESP32_SCH);
    const spiHspi = imported.hardware.resources['spi.hspi'];
    assert(spiHspi && spiHspi.available === true,
        'Mapped schematic SPI signals to canonical HyprAccel resource spi.hspi.');
} catch (e) {
    assert(false, `Semantic SPI mapping failed: ${e.message}`);
}

// 9. Semantic UART mapping
try {
    const imported = importKiCadSchematic(VALID_ESP32_SCH);
    const uart0 = imported.hardware.resources['uart.uart0'];
    assert(uart0 && imported.hardware.assignments.some(a => a.resource === 'uart.uart0' && a.role === 'tx' && a.pin === 'GPIO1'),
        'Mapped UART TX (GPIO1) signal to canonical HyprAccel resource uart.uart0.');
} catch (e) {
    assert(false, `Semantic UART mapping failed: ${e.message}`);
}

// 10. Semantic I2C mapping
try {
    const imported = importKiCadSchematic(VALID_ESP32_SCH);
    const i2c0 = imported.hardware.resources['i2c.i2c0'];
    assert(i2c0 && imported.hardware.assignments.some(a => a.resource === 'i2c.i2c0' && a.role === 'sda' && a.pin === 'GPIO21'),
        'Mapped I2C SDA (GPIO21) signal to canonical HyprAccel resource i2c.i2c0.');
} catch (e) {
    assert(false, `Semantic I2C mapping failed: ${e.message}`);
}

// 11. GPIO mapping
try {
    const imported = importKiCadSchematic(VALID_ESP32_SCH);
    assert(imported.hardware.assignments.some(a => a.resource === 'gpio.GPIO2' && a.role === 'gpio' && a.pin === 'GPIO2'),
        'Mapped GPIO2 signal to canonical HyprAccel resource gpio.GPIO2.');
} catch (e) {
    assert(false, `GPIO mapping failed: ${e.message}`);
}

// 12. Malformed schematic handling
try {
    parseKiCadSchematic(MALFORMED_SCH);
    assert(false, 'Did not throw error on malformed schematic text.');
} catch (e) {
    assert(e.message.includes('Malformed schematic') || e.message.includes('Unclosed parenthesis'),
        `Successfully caught malformed schematic error: "${e.message}".`);
}

console.log(`\n=== SUMMARY: ${passedTests}/${totalTests} TESTS PASSED ===\n`);

if (passedTests !== totalTests) {
    process.exit(1);
}
