#!/usr/bin/env node

/* PLAT-T1: dependency-free static server and SSE bridge for telemetry_feed. */
const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const root = __dirname;
const port = Number(process.env.PORT || 8080);
const files = new Map([
    ['/', ['index.html', 'text/html; charset=utf-8']],
    ['/index.html', ['index.html', 'text/html; charset=utf-8']],
    ['/dashboard.js', ['dashboard.js', 'application/javascript; charset=utf-8']],
    ['/styles.css', ['styles.css', 'text/css; charset=utf-8']],
]);

function streamTelemetry(request, response) {
    const executable = path.join(root, 'build', 'telemetry_feed');
    if (!fs.existsSync(executable)) {
        response.writeHead(503, { 'Content-Type': 'text/plain; charset=utf-8' });
        response.end('Telemetry source is not built. Run: make -C plat/dashboard build\n');
        return;
    }

    response.writeHead(200, {
        'Content-Type': 'text/event-stream',
        'Cache-Control': 'no-cache',
        Connection: 'keep-alive',
    });
    response.write('retry: 2000\n\n');
    const feed = spawn(executable, [], { stdio: ['ignore', 'pipe', 'pipe'] });
    let pending = '';
    feed.stdout.on('data', chunk => {
        pending += chunk.toString();
        const lines = pending.split('\n');
        pending = lines.pop();
        for (const line of lines) {
            if (line) response.write(`data: ${line}\n\n`);
        }
    });
    feed.stderr.on('data', chunk => console.error(`telemetry_feed: ${chunk}`));
    feed.on('error', error => console.error(`Unable to start telemetry_feed: ${error.message}`));
    request.on('close', () => feed.kill());
}

http.createServer((request, response) => {
    const requestPath = new URL(request.url, `http://${request.headers.host}`).pathname;
    if (requestPath === '/events') return streamTelemetry(request, response);
    const asset = files.get(requestPath);
    if (!asset) {
        response.writeHead(404);
        response.end('Not found\n');
        return;
    }
    const [file, contentType] = asset;
    fs.readFile(path.join(root, file), (error, content) => {
        if (error) {
            response.writeHead(500);
            response.end('Unable to read dashboard asset\n');
            return;
        }
        response.writeHead(200, { 'Content-Type': contentType });
        response.end(content);
    });
}).listen(port, () => console.log(`HyprAccel telemetry dashboard: http://localhost:${port}`));
