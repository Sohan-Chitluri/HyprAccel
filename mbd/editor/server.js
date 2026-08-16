/**
 * server.js — HyprAccel MBD Editor Dev Server
 * MBD-T1a / MBD-T1b / MBD-T5 / MBD-T6
 *
 * Endpoints:
 *   GET  /api/boards          → parsed boards.yaml as JSON
 *   POST /api/generate        → write pin assignments → call gen_board_config.js → return header
 *   GET  /                    → serves pin_config.html
 *   GET  /graph                → serves the node graph editor
 *   POST /api/build           → run graph_to_c.js for the current graph
 */

const express = require('express');
const fs      = require('fs');
const path    = require('path');
const os           = require('os');
const { execFileSync, execSync } = require('child_process');

const app  = express();
const PORT = Number(process.env.HYPRACCEL_EDITOR_PORT || 3737);

const REPO_ROOT    = path.resolve(__dirname, '../../');
const BOARDS_YAML  = path.join(REPO_ROOT, 'boards/boards.yaml');
const CODEGEN_JS   = path.join(REPO_ROOT, 'boards/codegen/gen_board_config.js');
const CODEGEN_OUT  = path.join(REPO_ROOT, 'boards/codegen');
const GRAPH_CODEGEN = path.join(REPO_ROOT, 'mbd/codegen/graph_to_c.js');

app.use(express.json());
app.use(express.static(path.join(__dirname, 'src')));

app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'src/pin_config.html'));
});

app.get('/graph', (req, res) => {
    res.sendFile(path.join(__dirname, 'src/graph_editor.html'));
});

/* --------------------------------------------------------------------------
 * YAML parser (mirrors the logic in gen_board_config.js — no npm yaml dep)
 * ----------------------------------------------------------------------- */
function parseSimpleYaml(content) {
    const lines = content.split('\n');
    const result = { boards: {} };
    let currentBoard = null;
    let currentCategory = null;
    let currentPinSection = null;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].split('#')[0].trimEnd();
        if (line.trim().length === 0) continue;

        const indent  = line.search(/\S/);
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

/* --------------------------------------------------------------------------
 * GET /api/boards
 * Returns parsed boards.yaml as JSON for the UI to build its pin picker.
 * ----------------------------------------------------------------------- */
app.get('/api/boards', (req, res) => {
    try {
        const yaml    = fs.readFileSync(BOARDS_YAML, 'utf8');
        const parsed  = parseSimpleYaml(yaml);
        res.json(parsed.boards);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

/* --------------------------------------------------------------------------
 * POST /api/generate
 * Body: { board: "thejas32", assignments: [ { node: "SensorInput[0]", peripheral: "spi0", pin: "SPI0MOSI", role: "mosi" }, ... ] }
 *
 * Writes an augmented boards.yaml (with assignments embedded as comments),
 * runs gen_board_config.js, and returns the generated header text plus a
 * machine-readable assignment block for MBD-T1b wiring.
 * ----------------------------------------------------------------------- */
app.post('/api/generate', (req, res) => {
    try {
        const { board, assignments } = req.body;
        if (!board || !Array.isArray(assignments)) {
            return res.status(400).json({ error: 'Missing board or assignments.' });
        }

        /* Run the existing codegen script */
        const cmd = `node "${CODEGEN_JS}" "${board}" "${BOARDS_YAML}" "${CODEGEN_OUT}"`;
        execSync(cmd, { stdio: 'pipe' });

        /* Read the generated base header */
        const headerPath  = path.join(CODEGEN_OUT, 'hyp_board_config.h');
        let   headerText  = fs.readFileSync(headerPath, 'utf8');

        /* Inject the MBD pin assignments as #defines — MBD-T1b wiring */
        if (assignments.length > 0) {
            const pinDefines = [
                '',
                '/* MBD Pin Assignments — generated by pin_config UI (MBD-T1b) */'
            ];
            for (const a of assignments) {
                const macroBase = a.node.replace(/[\[\]. ]/g, '_').toUpperCase();
                const pinName   = (a.pin || '').toUpperCase().replace(/[^A-Z0-9_]/g, '_');
                const periph    = (a.peripheral || '').toUpperCase().replace(/[^A-Z0-9_]/g, '_');
                pinDefines.push(`#define HYP_PIN_${macroBase}_${a.role.toUpperCase()} "${a.pin}"  /* ${a.node} → ${a.peripheral}.${a.role} */`);
                if (a.role === 'mosi' || a.role === 'tx' || a.role === 'sda') {
                    pinDefines.push(`#define HYP_PERIPH_${macroBase} "${a.peripheral}"`);
                }
            }
            headerText = headerText.replace(
                '#endif /* HYP_BOARD_CONFIG_H */',
                pinDefines.join('\n') + '\n\n#endif /* HYP_BOARD_CONFIG_H */'
            );
            fs.writeFileSync(headerPath, headerText, 'utf8');
        }

        res.json({ success: true, header: headerText, assignments });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

/* --------------------------------------------------------------------------
 * POST /api/build
 * Body: a hypraccel.mbd.graph object from graph_editor.html.
 * The existing graph_to_c.js remains the source of truth for validation and
 * generated C; this endpoint only supplies its temporary JSON/C file paths.
 * ----------------------------------------------------------------------- */
app.post('/api/build', (req, res) => {
    const graph = req.body;
    if (!graph || typeof graph !== 'object' || !Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
        return res.status(400).json({ error: 'Body must be a graph with nodes and edges arrays.' });
    }

    let tempDir;
    try {
        tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypraccel-mbd-build-'));
        const graphPath = path.join(tempDir, 'graph.json');
        const outputPath = path.join(tempDir, 'graph.c');
        fs.writeFileSync(graphPath, JSON.stringify(graph, null, 2), 'utf8');
        execFileSync(process.execPath, [GRAPH_CODEGEN, graphPath, outputPath], { encoding: 'utf8', stdio: 'pipe' });
        res.json({ success: true, graph, source: fs.readFileSync(outputPath, 'utf8') });
    } catch (err) {
        const detail = err.stderr ? String(err.stderr).trim() : err.message;
        res.status(422).json({ error: detail || 'Graph code generation failed.' });
    } finally {
        if (tempDir) {
            for (const file of ['graph.json', 'graph.c']) {
                try { fs.unlinkSync(path.join(tempDir, file)); } catch (_) { /* best-effort temp cleanup */ }
            }
            try { fs.rmdirSync(tempDir); } catch (_) { /* best-effort temp cleanup */ }
        }
    }
});

app.listen(PORT, () => {
    console.log(`HyprAccel MBD Editor  →  http://localhost:${PORT}`);
    console.log(`Boards YAML           →  ${BOARDS_YAML}`);
    console.log(`Codegen output        →  ${CODEGEN_OUT}/hyp_board_config.h`);
});
