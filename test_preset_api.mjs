/**
 * Test the preset API by directly calling the hardware endpoint
 */

async function testPresetFix() {
    // Set the board to esp32
    const hardwareConfig = {
        board: 'esp32',
        assignments: [],
        configurations: {},
        devices: []
    };
    
    // First, set up the hardware configuration with esp32 board
    const res = await fetch('http://127.0.0.1:3737/api/hardware', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(hardwareConfig)
    });
    
    const result = await res.json();
    console.log('Initial hardware config:', result.success ? 'OK' : result.error);
    
    // Now check what the hardware API returns for PWM resources
    const hwRes = await fetch('http://127.0.0.1:3737/api/hardware');
    const hw = await hwRes.json();
    
    console.log('\nBoard:', hw.board);
    console.log('PWM resources:');
    Object.values(hw.resources || {}).forEach(r => {
        if (r.type === 'pwm') {
            console.log(`  ${r.id}: available=${r.available}, config=`, r.configuration);
        }
    });
    
    console.log('\nPWM pins declared:', hw.resources ? Object.values(hw.resources).filter(r => r.type === 'pwm').map(r => r.instance) : 'none');
}

testPresetFix().catch(console.error);
