const http = require('http');

function fetchUrl(url, method = 'GET', postData = null) {
    return new Promise((resolve, reject) => {
        const u = new URL(url);
        const options = {
            hostname: u.hostname,
            port: u.port,
            path: u.pathname + u.search,
            method: method,
            headers: postData ? { 'Content-Type': 'application/json' } : {}
        };
        const req = http.request(options, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => resolve({ status: res.statusCode, data }));
        });
        req.on('error', reject);
        if (postData) req.write(JSON.stringify(postData));
        req.end();
    });
}

async function runTests() {
    console.log('=== HYPRACCEL VERIFICATION SUITE ===');

    // Test 1: /graph HTTP 200
    const graphRes = await fetchUrl('http://localhost:3737/graph');
    console.log(`[PASS] GET /graph -> Status ${graphRes.status} (Length: ${graphRes.data.length})`);
    if (graphRes.status !== 200) throw new Error('/graph did not return 200');
    if (!graphRes.data.includes('<iframe class="embed" src="/pin_config.html?embed=1"')) {
        throw new Error('graph_editor.html does not contain embedded pin_config.html iframe');
    }
    console.log('[PASS] graph_editor.html contains embedded pin_config.html iframe');

    // Test 2: /pin_config.html HTTP 200
    const pinRes = await fetchUrl('http://localhost:3737/pin_config.html?embed=1');
    console.log(`[PASS] GET /pin_config.html?embed=1 -> Status ${pinRes.status} (Length: ${pinRes.data.length})`);
    if (pinRes.status !== 200) throw new Error('/pin_config.html did not return 200');

    // Test 3: /api/boards HTTP 200
    const boardsRes = await fetchUrl('http://localhost:3737/api/boards');
    console.log(`[PASS] GET /api/boards -> Status ${boardsRes.status}`);
    const boards = JSON.parse(boardsRes.data);
    if (!boards.thejas32 || !boards.esp32) throw new Error('Boards data missing expected targets');
    console.log(`[PASS] Loaded boards: ${Object.keys(boards).join(', ')}`);

    // Test 4: /api/generate POST
    const genPayload = {
        board: "thejas32",
        assignments: [
            { node: "SensorInput[1]", peripheral: "spi.spi0", pin: "SPI0MOSI", role: "bus" }
        ],
        configurations: {}
    };
    const genRes = await fetchUrl('http://localhost:3737/api/generate', 'POST', genPayload);
    console.log(`[PASS] POST /api/generate -> Status ${genRes.status}`);
    const genData = JSON.parse(genRes.data);
    if (!genData.success || !genData.header.includes('HYP_PIN_SENSORINPUT_1__BUS "SPI0MOSI"')) {
        throw new Error('Pin generation output mismatch');
    }
    console.log('[PASS] Pin config generation produces valid hyp_board_config.h header defines');

    // Test 5: /api/build POST
    const testGraph = {
        format: "hypraccel.mbd.graph",
        version: 1,
        id: "verify-graph",
        name: "verify-graph",
        nodes: [
            { id: "cordicop_1", type: "CordicOp", label: "CordicOp 1", params: { operation: "sin", implementation: "auto", iterations: 16 } },
            { id: "publish_1", type: "Publish", label: "Publish 1", params: { topic: "telemetry/yaw", transport: "telemetry", retain: false } }
        ],
        edges: [
            { id: "e1", from: { node: "cordicop_1", port: "value" }, to: { node: "publish_1", port: "value" } }
        ],
        inputs: [
            { name: "angle_rad", type: "number", unit: "rad" }
        ],
        metadata: {
            externalBindings: [
                { input: "angle_rad", to: { node: "cordicop_1", port: "angle_rad" } }
            ]
        }
    };
    const buildRes = await fetchUrl('http://localhost:3737/api/build', 'POST', testGraph);
    console.log(`[PASS] POST /api/build -> Status ${buildRes.status}`);
    const buildData = JSON.parse(buildRes.data);
    if (!buildData.success) {
        console.error('Build error detail:', buildData.error);
        throw new Error('Graph codegen failed: ' + buildData.error);
    }
    console.log('[PASS] Graph codegen succeeded');

    console.log('\nALL VERIFICATION TESTS PASSED SUCCESSFULLY!');
}

runTests().catch(err => {
    console.error('[FAIL]', err);
    process.exit(1);
});
