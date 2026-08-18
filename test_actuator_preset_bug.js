/**
 * Test script to verify the actuator preset bug
 * This simulates the applyPreset logic from pin_config.html
 */

// Simulated board data (esp32)
const bd = {
    name: "ESP32 (WROOM-32)",
    pins: {
        pwm: ["GPIO25", "GPIO26", "GPIO27", "GPIO32", "GPIO33"]
    }
};

// Simulated state
let nodes = [];
let assignments = {};
let nodeCounter = 0;

const SEMANTIC_SIGNAL_ROLES = ['sck', 'mosi', 'miso', 'cs', 'tx', 'rx', 'sda', 'scl', 'output', 'input', 'gpio'];

function boardHasPin(board, pinName) {
    if (!board || !board.pins) return false;
    for (const secVal of Object.values(board.pins)) {
        if (Array.isArray(secVal)) {
            if (secVal.includes(pinName)) return true;
        }
    }
    return false;
}

function applyServoPwmPreset() {
    console.log("\n=== Applying Servo/PWM Preset ===");
    
    // Check for existing PWM node (THE BUG)
    const existingPwmNode = nodes.find(nd => nd.type === 'ActuatorOutput' &&
        nd.slots.some(sl => sl.peripheral && sl.peripheral.startsWith('pwm.') && sl.pin));
    if (existingPwmNode) {
        console.log(`❌ BUG TRIGGERED: Preset already applied to ${existingPwmNode.label}, refusing to apply again`);
        return false;
    }

    const pwmPins = Array.isArray(bd.pins && bd.pins.pwm) ? bd.pins.pwm : [];
    const availablePwmPin = pwmPins.find(pin => !assignments[pin]);
    if (!availablePwmPin) {
        console.log(`❌ No unassigned PWM-capable pin`);
        return false;
    }

    console.log(`Available PWM pin: ${availablePwmPin}`);
    
    const presetSlots = [{ role: 'output', pin: availablePwmPin, peripheral: `pwm.${availablePwmPin}` }];
    
    // Find an empty ActuatorOutput node (no pins assigned)
    let presetNode = nodes.find(nd => nd.type === 'ActuatorOutput' && nd.slots.every(sl => !sl.pin)) || null;
    
    const newNode = presetNode || {
        id: ++nodeCounter,
        type: 'ActuatorOutput',
        label: `ActuatorOutput[${nodeCounter}]`,
        slots: SEMANTIC_SIGNAL_ROLES.map(role => ({ role, pin: null, peripheral: null }))
    };

    for (const pSlot of presetSlots) {
        const slot = newNode.slots.find(s => s.role === pSlot.role);
        if (slot) {
            slot.pin = pSlot.pin;
            slot.peripheral = pSlot.peripheral;
            assignments[pSlot.pin] = `${newNode.id}-${pSlot.role}`;
        }
    }

    if (!presetNode) nodes.push(newNode);
    
    console.log(`✓ Applied to ${newNode.label} with pin ${availablePwmPin}`);
    return true;
}

// Test Scenario: Two ActuatorOutput nodes should each get their own PWM preset
console.log("=== Test: Two ActuatorOutput nodes with Servo/PWM preset ===\n");

// Add first ActuatorOutput node
nodes.push({
    id: ++nodeCounter,
    type: 'ActuatorOutput',
    label: `ActuatorOutput[${nodeCounter}]`,
    slots: SEMANTIC_SIGNAL_ROLES.map(role => ({ role, pin: null, peripheral: null }))
});
console.log("Created ActuatorOutput[1]");

// Apply preset to first node
applyServoPwmPreset();

// Add second ActuatorOutput node  
nodes.push({
    id: ++nodeCounter,
    type: 'ActuatorOutput',
    label: `ActuatorOutput[${nodeCounter}]`,
    slots: SEMANTIC_SIGNAL_ROLES.map(role => ({ role, pin: null, peripheral: null }))
});
console.log("Created ActuatorOutput[2]");

// Apply preset to second node
applyServoPwmPreset();

console.log("\n=== Final State ===");
nodes.forEach(n => {
    const pwmSlots = n.slots.filter(s => s.pin && s.peripheral?.startsWith('pwm.'));
    console.log(`${n.label}: ${pwmSlots.length} PWM assignment(s)`);
    pwmSlots.forEach(s => console.log(`  - ${s.role}: ${s.pin} (${s.peripheral})`));
});

console.log("\nAssignments:", assignments);

// Expected: Both nodes should have PWM assignments on different pins
// Actual (with bug): Second application fails
