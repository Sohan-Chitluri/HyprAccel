/**
 * Integration test: simulate full preset application flow
 */

async function testIntegration() {
    // 1. Set up hardware config with esp32 board
    const hardwareConfig = {
        board: 'esp32',
        assignments: [],
        configurations: {},
        devices: []
    };
    
    const res = await fetch('http://127.0.0.1:3737/api/hardware', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(hardwareConfig)
    });
    
    const result = await res.json();
    console.log('1. Hardware config:', result.success ? 'OK' : result.error);
    
    // 2. Get hardware to see PWM resources
    const hwRes = await fetch('http://127.0.0.1:3737/api/hardware');
    const hw = await hwRes.json();
    
    const pwmResources = Object.values(hw.resources || {}).filter(r => r.type === 'pwm' && r.available);
    console.log(`\n2. Available PWM resources: ${pwmResources.length}`);
    pwmResources.forEach(r => console.log(`   ${r.id}`));
    
    // 3. Simulate applying servo_pwm preset multiple times
    // The preset should create ActuatorOutput nodes with different PWM pins
    const pwmPins = pwmResources.map(r => r.instance);
    const assignments = {};
    let nodes = [];
    let nodeCounter = 0;
    const SEMANTIC_SIGNAL_ROLES = ['sck', 'mosi', 'miso', 'cs', 'tx', 'rx', 'sda', 'scl', 'output', 'input', 'gpio'];
    
    function applyServoPwmPreset() {
        const availablePwmPin = pwmPins.find(pin => !assignments[pin]);
        if (!availablePwmPin) {
            console.log(`   ❌ No unassigned PWM pin available`);
            return false;
        }
        
        // NEW FIX: Find an ActuatorOutput node that doesn't already have a PWM assignment
        let presetNode = nodes.find(nd => nd.type === 'ActuatorOutput' &&
            nd.slots.every(sl => !(sl.peripheral && sl.peripheral.startsWith('pwm.') && sl.pin))) || null;
        
        const newNode = presetNode || {
            id: ++nodeCounter,
            type: 'ActuatorOutput',
            label: `ActuatorOutput[${nodeCounter}]`,
            slots: SEMANTIC_SIGNAL_ROLES.map(role => ({ role, pin: null, peripheral: null }))
        };
        
        const slot = newNode.slots.find(s => s.role === 'output');
        if (slot) {
            slot.pin = availablePwmPin;
            slot.peripheral = `pwm.${availablePwmPin}`;
            assignments[availablePwmPin] = `${newNode.id}-output`;
        }
        
        if (!presetNode) nodes.push(newNode);
        
        console.log(`   ✓ Applied to ${newNode.label} with pin ${availablePwmPin}`);
        return true;
    }
    
    console.log('\n3. Applying servo_pwm preset 3 times:');
    applyServoPwmPreset(); // First application
    applyServoPwmPreset(); // Second application
    applyServoPwmPreset(); // Third application
    
    console.log('\n4. Final node states:');
    nodes.forEach(n => {
        const pwmSlots = n.slots.filter(s => s.pin && s.peripheral?.startsWith('pwm.'));
        console.log(`   ${n.label}: ${pwmSlots.length} PWM assignment(s)`);
        pwmSlots.forEach(s => console.log(`      - ${s.role}: ${s.pin} (${s.peripheral})`));
    });
    
    // 5. Verify all nodes have unique PWM assignments
    const allUnique = Object.keys(assignments).length === nodes.length;
    console.log(`\n5. All nodes have unique PWM assignments: ${allUnique ? '✓ PASS' : '❌ FAIL'}`);
    console.log(`   Total nodes: ${nodes.length}, Total assignments: ${Object.keys(assignments).length}`);
    
    // 6. Test that graph editor would see the hardware resources correctly
    console.log('\n6. Graph editor hardware resource validation:');
    const hwForGraph = await fetch('http://127.0.0.1:3737/api/hardware');
    const hwGraph = await hwForGraph.json();
    
    // Check that each node can select its own PWM resource
    nodes.forEach(n => {
        const pwmSlot = n.slots.find(s => s.peripheral?.startsWith('pwm.'));
        if (pwmSlot) {
            const resource = hwGraph.resources[pwmSlot.peripheral];
            console.log(`   ${n.label} -> ${pwmSlot.peripheral}: ${resource ? 'AVAILABLE' : 'NOT FOUND'}`);
        }
    });
    
    console.log('\n=== INTEGRATION TEST COMPLETE ===');
}

testIntegration().catch(console.error);
