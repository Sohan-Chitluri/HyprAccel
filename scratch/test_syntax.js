const fs = require('fs');
const path = require('path');

console.log('Testing JS syntax in graph_editor.html and pin_config.html...');

function checkHtmlJs(filePath) {
    const content = fs.readFileSync(filePath, 'utf8');
    const scripts = content.match(/<script[\s\S]*?>([\s\S]*?)<\/script>/gi) || [];
    console.log(`Found ${scripts.length} script tags in ${path.basename(filePath)}`);
    scripts.forEach((scriptTag, idx) => {
        const code = scriptTag.replace(/<script[\s\S]*?>/i, '').replace(/<\/script>/i, '');
        try {
            new Function(code);
            console.log(`  Script ${idx + 1}: Syntax OK`);
        } catch (err) {
            console.error(`  Script ${idx + 1}: Syntax Error:`, err.message);
        }
    });
}

checkHtmlJs('/home/peskybird/Projects/HyprAccel/mbd/editor/src/graph_editor.html');
checkHtmlJs('/home/peskybird/Projects/HyprAccel/mbd/editor/src/pin_config.html');
