/**
 * kicad_parser.js — KiCad .kicad_sch S-expression parser (SCH-T1)
 *
 * Parses modern KiCad 6/7/8 S-expression schematic files into a structured AST
 * and extracts raw symbols, pins, wires, labels, and schematic metadata.
 */

/**
 * Tokenize S-expression text into tokens.
 * @param {string} text
 * @returns {Array<string>}
 */
function tokenizeSExpr(text) {
    if (typeof text !== 'string') {
        throw new Error('Invalid input: schematic content must be a string.');
    }
    const tokens = [];
    let i = 0;
    const len = text.length;

    while (i < len) {
        const char = text[i];

        // Skip whitespace
        if (/\s/.test(char)) {
            i++;
            continue;
        }

        // Left paren
        if (char === '(') {
            tokens.push('(');
            i++;
            continue;
        }

        // Right paren
        if (char === ')') {
            tokens.push(')');
            i++;
            continue;
        }

        // Quoted string
        if (char === '"') {
            i++;
            let str = '';
            let escaped = false;
            while (i < len) {
                const c = text[i];
                if (escaped) {
                    str += c;
                    escaped = false;
                } else if (c === '\\') {
                    escaped = true;
                } else if (c === '"') {
                    break;
                } else {
                    str += c;
                }
                i++;
            }
            if (i >= len && text[i - 1] !== '"') {
                throw new Error('Malformed schematic: Unterminated string literal.');
            }
            tokens.push(str);
            i++;
            continue;
        }

        // Atom (unquoted word, number, identifier)
        let atom = '';
        while (i < len && !/\s/.test(text[i]) && text[i] !== '(' && text[i] !== ')') {
            atom += text[i];
            i++;
        }
        if (atom.length > 0) {
            tokens.push(atom);
        }
    }

    return tokens;
}

/**
 * Parse tokens into an S-expression tree.
 * @param {Array<string>} tokens
 * @returns {Array}
 */
function parseSExprTree(tokens) {
    if (!tokens || tokens.length === 0) {
        throw new Error('Malformed schematic: Empty token stream.');
    }

    const stack = [[]];

    for (let idx = 0; idx < tokens.length; idx++) {
        const tok = tokens[idx];
        if (tok === '(') {
            const newList = [];
            stack[stack.length - 1].push(newList);
            stack.push(newList);
        } else if (tok === ')') {
            if (stack.length <= 1) {
                throw new Error('Malformed schematic: Unexpected closing parenthesis \')\'.');
            }
            stack.pop();
        } else {
            stack[stack.length - 1].push(tok);
        }
    }

    if (stack.length !== 1) {
        throw new Error('Malformed schematic: Unclosed parenthesis \'(\'.');
    }

    const rootList = stack[0];
    if (rootList.length === 0 || !Array.isArray(rootList[0])) {
        throw new Error('Malformed schematic: No root S-expression found.');
    }

    return rootList[0];
}

/**
 * Extract child node from AST list matching a specific tag.
 */
function findChild(astNode, tag) {
    if (!Array.isArray(astNode)) return null;
    for (let i = 1; i < astNode.length; i++) {
        const child = astNode[i];
        if (Array.isArray(child) && child[0] === tag) {
            return child;
        }
    }
    return null;
}

/**
 * Extract all child nodes matching a specific tag.
 */
function findChildren(astNode, tag) {
    const results = [];
    if (!Array.isArray(astNode)) return results;
    for (let i = 1; i < astNode.length; i++) {
        const child = astNode[i];
        if (Array.isArray(child) && child[0] === tag) {
            results.push(child);
        }
    }
    return results;
}

/**
 * Extract coordinates (xy X Y) from a node.
 */
function extractAt(astNode) {
    const atNode = findChild(astNode, 'at');
    if (!atNode) return { x: 0, y: 0, angle: 0 };
    return {
        x: parseFloat(atNode[1]) || 0,
        y: parseFloat(atNode[2]) || 0,
        angle: parseFloat(atNode[3]) || 0
    };
}

/**
 * Extract pins from a lib_symbols symbol definition node recursively.
 */
function extractLibSymbolPins(symbolDefNode) {
    const pins = [];

    function traverse(node) {
        if (!Array.isArray(node)) return;
        if (node[0] === 'pin') {
            const electricalType = typeof node[1] === 'string' ? node[1] : 'passive';
            const nameNode = findChild(node, 'name');
            const numNode = findChild(node, 'number');
            const atNode = findChild(node, 'at');

            pins.push({
                type: electricalType,
                name: nameNode && nameNode[1] ? String(nameNode[1]) : '',
                number: numNode && numNode[1] ? String(numNode[1]) : '',
                at: atNode ? { x: parseFloat(atNode[1]) || 0, y: parseFloat(atNode[2]) || 0 } : { x: 0, y: 0 }
            });
        }
        for (let i = 1; i < node.length; i++) {
            if (Array.isArray(node[i])) {
                traverse(node[i]);
            }
        }
    }

    traverse(symbolDefNode);
    return pins;
}

/**
 * Parse a KiCad schematic text into a structured schematic representation.
 * @param {string} content - Raw .kicad_sch text
 * @returns {Object} Structured schematic representation
 */
function parseKiCadSchematic(content) {
    const tokens = tokenizeSExpr(content);
    const ast = parseSExprTree(tokens);

    if (!Array.isArray(ast) || ast[0] !== 'kicad_sch') {
        throw new Error('Invalid KiCad schematic: Root expression must be (kicad_sch ...)');
    }

    // 1. Metadata
    const versionNode = findChild(ast, 'version');
    const generatorNode = findChild(ast, 'generator');
    const uuidNode = findChild(ast, 'uuid');
    const paperNode = findChild(ast, 'paper');
    const titleBlockNode = findChild(ast, 'title_block');

    const metadata = {
        version: versionNode ? String(versionNode[1]) : 'unknown',
        generator: generatorNode ? String(generatorNode[1]) : 'unknown',
        uuid: uuidNode ? String(uuidNode[1]) : '',
        paper: paperNode ? String(paperNode[1]) : '',
        titleBlock: {
            title: '',
            company: '',
            revision: '',
            date: ''
        }
    };

    if (titleBlockNode) {
        const title = findChild(titleBlockNode, 'title');
        const company = findChild(titleBlockNode, 'company');
        const rev = findChild(titleBlockNode, 'rev');
        const date = findChild(titleBlockNode, 'date');
        if (title) metadata.titleBlock.title = String(title[1] || '');
        if (company) metadata.titleBlock.company = String(company[1] || '');
        if (rev) metadata.titleBlock.revision = String(rev[1] || '');
        if (date) metadata.titleBlock.date = String(date[1] || '');
    }

    // 2. Library Symbols (lib_symbols)
    const libSymbols = {};
    const libSymbolsNode = findChild(ast, 'lib_symbols');
    if (libSymbolsNode) {
        const symDefs = findChildren(libSymbolsNode, 'symbol');
        for (const symDef of symDefs) {
            const libId = String(symDef[1] || '');
            if (libId) {
                libSymbols[libId] = {
                    libId,
                    pins: extractLibSymbolPins(symDef)
                };
            }
        }
    }

    // 3. Symbol Instances
    const rawSymbols = findChildren(ast, 'symbol');
    const symbols = [];

    for (const symNode of rawSymbols) {
        const libIdNode = findChild(symNode, 'lib_id');
        const symUuidNode = findChild(symNode, 'uuid');
        const unitNode = findChild(symNode, 'unit');
        const at = extractAt(symNode);

        const properties = {};
        const propNodes = findChildren(symNode, 'property');
        for (const pNode of propNodes) {
            const pName = String(pNode[1] || '');
            const pVal = String(pNode[2] || '');
            if (pName) {
                properties[pName] = pVal;
            }
        }

        const instancePins = [];
        const pinNodes = findChildren(symNode, 'pin');
        for (const pNode of pinNodes) {
            const pNum = String(pNode[1] || '');
            const pUuidNode = findChild(pNode, 'uuid');
            const pAt = extractAt(pNode);
            instancePins.push({
                number: pNum,
                uuid: pUuidNode ? String(pUuidNode[1]) : '',
                at: pAt
            });
        }

        const libId = libIdNode ? String(libIdNode[1]) : '';
        symbols.push({
            libId,
            uuid: symUuidNode ? String(symUuidNode[1]) : '',
            unit: unitNode ? parseInt(unitNode[1], 10) : 1,
            at,
            properties,
            reference: properties['Reference'] || '',
            value: properties['Value'] || '',
            footprint: properties['Footprint'] || '',
            instancePins,
            libDef: libSymbols[libId] || null
        });
    }

    // 4. Wires
    const wireNodes = findChildren(ast, 'wire');
    const wires = [];
    for (const wNode of wireNodes) {
        const ptsNode = findChild(wNode, 'pts');
        const wUuid = findChild(wNode, 'uuid');
        const pts = [];
        if (ptsNode) {
            const xyNodes = findChildren(ptsNode, 'xy');
            for (const xy of xyNodes) {
                pts.push({
                    x: parseFloat(xy[1]) || 0,
                    y: parseFloat(xy[2]) || 0
                });
            }
        }
        wires.push({
            pts,
            uuid: wUuid ? String(wUuid[1]) : ''
        });
    }

    // 5. Labels (local label, global_label, hierarchical_label)
    const labels = [];
    for (const labelType of ['label', 'global_label', 'hierarchical_label']) {
        const lNodes = findChildren(ast, labelType);
        for (const lNode of lNodes) {
            const name = String(lNode[1] || '');
            const lUuid = findChild(lNode, 'uuid');
            const at = extractAt(lNode);
            labels.push({
                type: labelType,
                name,
                at,
                uuid: lUuid ? String(lUuid[1]) : ''
            });
        }
    }

    // 6. Explicit Nets (net directives if present)
    const netNodes = findChildren(ast, 'net');
    const nets = [];
    for (const nNode of netNodes) {
        const numNode = findChild(nNode, 'number');
        const nameNode = findChild(nNode, 'name');
        if (numNode && nameNode) {
            nets.push({
                number: parseInt(numNode[1], 10),
                name: String(nameNode[1])
            });
        }
    }

    return {
        metadata,
        libSymbols,
        symbols,
        wires,
        labels,
        nets,
        rawAst: ast
    };
}

module.exports = {
    tokenizeSExpr,
    parseSExprTree,
    parseKiCadSchematic
};
