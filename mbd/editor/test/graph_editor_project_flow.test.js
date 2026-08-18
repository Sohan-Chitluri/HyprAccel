'use strict';

/* Lifecycle guard for the browser-only project chooser/generation path. */
const assert = require('assert');
const fs = require('fs');

const source = fs.readFileSync(require.resolve('../src/graph_editor.html'), 'utf8');
const resolverStart = source.indexOf('async function resolveProjectForGeneration()');
const generateStart = source.indexOf('async function generateProject()');
const generateEnd = source.indexOf('\nfunction confirmDelete', generateStart);
assert(resolverStart >= 0 && generateStart > resolverStart && generateEnd > generateStart);

const resolver = source.slice(resolverStart, generateStart);
const generate = source.slice(generateStart, generateEnd);
assert.match(resolver, /fetch\('\/api\/projects'\)/);
assert.match(resolver, /window\.prompt\(`Select a project by ID/);
assert.match(resolver, /window\.prompt\('No projects exist/);
assert.match(resolver, /fetch\('\/api\/projects', \{[\s\S]*method: 'POST'/);
assert.match(resolver, /localStorage\.setItem\('hypraccel_active_project_id', activeProjectId\)/);

const saveIndex = generate.indexOf('if (!await saveGraph()) return;');
const generateIndex = generate.indexOf("fetch(`/api/projects/");
const workspaceIndex = generate.indexOf('window.open(`/workspace?projectId=');
assert(saveIndex >= 0 && generateIndex > saveIndex, 'graph must save before generation');
assert(workspaceIndex > generateIndex, 'workspace must open after generation');
assert.match(generate, /Generation failed: \$\{err\.message\}/);

console.log('graph_editor_project_flow.test.js: PASS');
