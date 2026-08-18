const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const REPO_ROOT = process.cwd();
const GRAPH_CODEGEN = path.join(REPO_ROOT, 'mbd/codegen/graph_to_c.js');
const ESP32_PROJECT = path.join(REPO_ROOT, 'mbd/esp32');
const ESP32_GENERATED = path.join(ESP32_PROJECT, 'generated');
const GRAPH_JSON = path.join(REPO_ROOT, 'mbd/schema/examples/cordic-to-publish.graph.json');

const graph = JSON.parse(fs.readFileSync(GRAPH_JSON, 'utf8'));

const tempDir = fs.mkdtempSync(path.join(require('os').tmpdir(), 'hypraccel-esp32-'));
const graphPath = path.join(tempDir, 'graph.json');
const graphSource = path.join(tempDir, 'graph.c');
fs.writeFileSync(graphPath, JSON.stringify(graph, null, 2), 'utf8');

const gen = spawnSync(process.execPath, [GRAPH_CODEGEN, graphPath, graphSource], { encoding: 'utf8' });
if (gen.status !== 0) {
    console.error('Graph codegen failed:', gen.stderr);
    process.exit(1);
}

const source = fs.readFileSync(graphSource, 'utf8');
const step = `hyp_graph_${graph.id.replace(/-/g, '_')}_step`;

fs.rmSync(ESP32_GENERATED, { recursive: true, force: true });
fs.mkdirSync(ESP32_GENERATED, { recursive: true });
fs.writeFileSync(path.join(ESP32_GENERATED, 'graph.c'), source, 'utf8');

for (const file of ['hyp_esp32.c', 'hyp_router.c', 'hyp_cordic_ref.c', 'hyp_cordic_ref.h', 'hyp_esp32_hw.cpp']) {
    fs.copyFileSync(path.join(REPO_ROOT, 'sdk/src', file), path.join(ESP32_GENERATED, file));
}
fs.copyFileSync(path.join(REPO_ROOT, 'sdk/include/hyprccel.h'), path.join(ESP32_GENERATED, 'hyprccel.h'));
fs.copyFileSync(path.join(REPO_ROOT, 'sdk/include/hyp_esp32_hw.h'), path.join(ESP32_GENERATED, 'hyp_esp32_hw.h'));
fs.copyFileSync(path.join(REPO_ROOT, 'boards/codegen/hyp_board_config.h'), path.join(ESP32_GENERATED, 'hyp_board_config.h'));

fs.writeFileSync(path.join(ESP32_GENERATED, 'main.cpp'), `/* Generated MBD-T9 runtime wrapper */
#include <Arduino.h>
#include "hyprccel.h"
#include "hyp_esp32_hw.h"

extern "C" int hyp_graph_init(void);
extern "C" void ${step}(float angle_rad);

extern "C" void hyp_esp32_publish(const char *topic, const void *data, uint32_t size)
{
    Serial.print("HYP_PUBLISH topic=");
    Serial.print(topic ? topic : "(null)");
    if (data && size == sizeof(float)) {
        Serial.print(" value=");
        Serial.print(*static_cast<const float *>(data), 6);
    }
    Serial.print(" bytes=");
    Serial.println(size);
}

void setup()
{
    Serial.begin(115200);
    delay(250);
    Serial.println("HYPRACCEL_MBD_T9_READY");
    Serial.print("HYPRACCEL_GRAPH_ID=");
    Serial.println("${graph.id}");

    int hw_status = hyp_esp32_hw_init();
    if (hw_status == 0) {
        Serial.println("HYPRACCEL_HW_INIT_OK");
        if (hyp_graph_init() == 0) {
            Serial.println("HYPRACCEL_INIT_OK");
        } else {
            Serial.println("HYPRACCEL_INIT_FAILED");
        }
    } else {
        Serial.print("[ERROR] ESP32 hardware initialization failed with code: ");
        Serial.println(hw_status);
        Serial.println("HYPRACCEL_INIT_FAILED");
    }
}

void loop()
{
    ${step}(1.0f);
    delay(1000);
}
`, 'utf8');

fs.rmSync(tempDir, { recursive: true, force: true });
console.log('Materialization succeeded in', ESP32_GENERATED);
