/**
 * easyeda_parser.js — EasyEDA .tel Netlist Parser (SCH-T8)
 *
 * Parses EasyEDA Standard/Tel netlist files into a structured representation
 * compatible with the existing HyprAccel canonical schematic model.
 *
 * The .tel format contains:
 * - $PACKAGES: Component footprint, value, and reference designators (multi-line format)
 * - $NETS: Net names and component.pin connections
 * - $END: End marker
 */

/**
 * Parse EasyEDA .tel netlist content into structured schematic representation.
 * @param {string} content - Raw .tel file content
 * @returns {Object} Structured schematic representation compatible with net_extractor
 */
function parseEasyEDANetlist(content) {
    if (typeof content !== 'string') {
        throw new Error('Invalid input: netlist content must be a string.');
    }

    const lines = content.split('\n');
    let currentSection = null;
    let sawPackages = false;
    let sawNets = false;
    const packages = [];  // Component instances from $PACKAGES
    const nets = [];      // Net connections from $NETS

    // Track component definitions for pin lookup
    const componentDefs = new Map(); // ref -> { footprint, value, pins: [] }

    let i = 0;
    while (i < lines.length) {
        const rawLine = lines[i];
        const line = rawLine.trim();

        // Skip empty lines
        if (line.length === 0) {
            i++;
            continue;
        }

        // Section markers
        if (line.startsWith('$')) {
            currentSection = line.slice(1).trim().toUpperCase(); // e.g., "PACKAGES", "NETS", "END"
            if (currentSection === 'PACKAGES') sawPackages = true;
            if (currentSection === 'NETS') sawNets = true;
            if (currentSection === 'END' || currentSection === 'ENDLIST') break;
            i++;
            continue;
        }

        if (currentSection === 'PACKAGES') {
            i = parsePackageLine(lines, i, packages, componentDefs);
        } else if (currentSection === 'NETS') {
            parseNetLine(line, nets);
            i++;
        } else {
            i++;
        }
    }

    if (!sawPackages || !sawNets) {
        throw new Error('Malformed EasyEDA netlist: expected $PACKAGES and $NETS sections.');
    }

    return {
        metadata: {
            source: 'easyeda_tel',
            version: '1.0',
            generator: 'EasyEDA'
        },
        components: packages,  // Component instances
        componentDefs,         // Component definitions (footprint, value, pins)
        nets,
        rawLines: lines
    };
}

function unquote(text) {
    return String(text || '').trim().replace(/^['"]|['"]$/g, '');
}

function parsePackageDescriptor(text) {
    const parts = String(text || '')
        .replace(/,$/, '')
        .split('!')
        .map(part => unquote(part));

    const packageName = parts[0] || '';
    const device = parts.length >= 2 ? parts[1] : '';
    const value = parts.length >= 3 ? parts[2] : (parts.length === 2 ? parts[1] : '');

    return { packageName, device, value };
}

/**
 * Parse package lines from $PACKAGES section, handling multi-line format.
 * Format examples:
 *   C0603 ! C0603 ! '0.1uf' ; C6 C7 C8 C9 C13
 *   ,  (continuation line - different footprint for same components)
 *         FOOTPRINT ! FOOTPRINT ! VALUE ,
 *         ; REFS
 *
 * Returns the next line index to process.
 */
function parsePackageLine(lines, startIdx, packages, componentDefs) {
    let i = startIdx;
    const line = lines[i].trim();

    // Handle continuation lines starting with comma
    if (line.startsWith(',')) {
        // This is a continuation of the previous package definition
        // Format:
        //   ,
        //         FOOTPRINT ! FOOTPRINT ! VALUE ,
        //         ; REFS
        i++;
        if (i >= lines.length) return i;

        // Read the footprint line (with comma at end)
        const footprintLine = lines[i].trim();
        i++;
        if (i >= lines.length) return i;

        // Read the references line (starts with ;)
        const refsLine = lines[i].trim();
        i++;

        const { packageName, device, value } = parsePackageDescriptor(footprintLine);

        // Parse references from refsLine (starts with ;)
        const refsStr = refsLine.startsWith(';') ? refsLine.substring(1).trim() : refsLine.trim();
        const refs = refsStr.split(/\s+/).filter(r => r.length > 0);

        // Create component instances for each reference
        for (const ref of refs) {
            componentDefs.set(ref, { package: packageName, device, value, pins: [], refs: [...refs] });
            packages.push({
                reference: ref,
                package: packageName,
                device,
                value,
                libId: packageName
            });
        }

        return i;
    }

    // Normal single-line package: FOOTPRINT ! FOOTPRINT ! VALUE ; REFS
    const semicolonIdx = line.indexOf(';');
    if (semicolonIdx === -1) {
        // Malformed line, skip
        return i + 1;
    }

    const beforeSemi = line.substring(0, semicolonIdx).trim();
    const afterSemi = line.substring(semicolonIdx + 1).trim();

    // Parse references
    const refs = afterSemi.split(/\s+/).filter(r => r.length > 0);

    // Parse footprint and value from beforeSemi
    // Format: FOOTPRINT ! FOOTPRINT ! VALUE
    const { packageName, device, value } = parsePackageDescriptor(beforeSemi);

    // Create component definition entry
    const compDef = {
        package: packageName,
        device,
        value,
        pins: [],
        refs
    };

    // Create component instances for each reference
    for (const ref of refs) {
        componentDefs.set(ref, { ...compDef });
        packages.push({
            reference: ref,
            package: packageName,
            device,
            value,
            libId: packageName
        });
    }

    // If this line ends with comma, the next lines are continuations
    // They will be handled on the next iteration since they start with ','
    return i + 1;
}

/**
 * Parse a net line from $NETS section.
 * Format: 'NET_NAME' ; COMPONENT.PIN COMPONENT.PIN ...
 * Example: 'SCL' ; Baro1.4 MPU.23 Wroom.22
 */
function parseNetLine(line, nets) {
    // Net name is in single quotes
    const quoteMatch = line.match(/'([^']+)'/);
    if (!quoteMatch) {
        // Try without quotes (some net names might not be quoted)
        const parts = line.split(';');
        if (parts.length >= 2) {
            const netName = parts[0].trim();
            const connections = parseConnections(parts[1]);
            if (connections.length > 0) {
                nets.push({ name: netName, connections });
            }
        }
        return;
    }

    const netName = quoteMatch[1];
    const afterQuote = line.substring(quoteMatch[0].length).trim();

    // Should start with semicolon
    if (!afterQuote.startsWith(';')) {
        return;
    }

    const connectionsStr = afterQuote.substring(1).trim();
    const connections = parseConnections(connectionsStr);

    if (connections.length > 0) {
        nets.push({ name: netName, connections });
    }
}

/**
 * Parse connection string: COMPONENT.PIN COMPONENT.PIN ...
 * Component references may contain dots and dashes.
 * Pin may be numeric or alphanumeric depending on the source netlist.
 */
function parseConnections(str) {
    const connections = [];
    const tokens = str.split(/\s+/).filter(t => t.length > 0);

    for (const token of tokens) {
        // Split on last dot to separate component from pin
        const lastDotIdx = token.lastIndexOf('.');
        if (lastDotIdx === -1) continue;

        const componentRef = token.substring(0, lastDotIdx);
        const pinNumber = token.substring(lastDotIdx + 1);

        if (!pinNumber) continue;

        connections.push({
            component: componentRef,
            pin: pinNumber
        });
    }

    return connections;
}

/**
 * Convert parsed EasyEDA netlist to the canonical format expected by net_extractor.
 * Creates a normalized model (Symbol, Pin, Net) directly from explicit net connections.
 */
function easyedaToNormalizedModel(parsedEasyEDA) {
    const symbolsMap = new Map();
    const allPins = [];
    const pinsMap = new Map();
    const netsMap = new Map();

    // 1. Gather all pins from net connections
    const pinConnections = new Map(); // key: "ref:pin" -> { component, pin, nets: [] }

    for (const net of parsedEasyEDA.nets) {
        for (const conn of net.connections) {
            const key = `${conn.component}:${conn.pin}`;
            if (!pinConnections.has(key)) {
                pinConnections.set(key, {
                    component: conn.component,
                    pin: conn.pin,
                    nets: []
                });
            }
            pinConnections.get(key).nets.push(net.name);
        }
    }

    // 2. Create symbol objects for each component
    for (const comp of parsedEasyEDA.components) {
        const reference = comp.reference;
        const value = comp.value || '';
        const packageName = comp.package || comp.footprint || '';
        const device = comp.device || '';
        const libId = comp.libId || packageName || device;
        const uuid = `sym-${reference}`;

        // Get all pins for this component from connections
        const compPins = [];
        for (const [key, conn] of pinConnections) {
            if (conn.component === reference) {
                compPins.push({
                    number: conn.pin,
                    nets: [...conn.nets]
                });
            }
        }

        const symbolObj = {
            uuid,
            reference,
            value,
            libId,
            unit: 1,
            footprint: packageName,
            properties: {
                Reference: reference,
                Value: value,
                Footprint: packageName,
                Package: packageName,
                Device: device
            },
            at: { x: 0, y: 0, angle: 0 },
            pins: [],
            instancePins: []
        };

        // Create pin objects
        for (const pinInfo of compPins) {
            const pNum = String(pinInfo.number);
            const pUuid = `${uuid}-pin-${pNum}`;

            const pinObj = {
                number: pNum,
                name: pNum,  // EasyEDA .tel doesn't provide pin names, only numbers
                uuid: pUuid,
                symbolRef: reference,
                symbolUuid: uuid,
                position: { x: 0, y: 0 },  // No position info in .tel
                nets: [...pinInfo.nets]
            };

            symbolObj.pins.push(pinObj);
            symbolObj.instancePins.push({
                number: pNum,
                uuid: pUuid,
                at: { x: 0, y: 0 }
            });

            allPins.push(pinObj);
            pinsMap.set(pUuid, pinObj);
            pinsMap.set(`${reference}:${pNum}`, pinObj);
        }

        symbolsMap.set(reference, symbolObj);
    }

    // Also add components that have no net connections (unconnected components)
    for (const [ref, def] of parsedEasyEDA.componentDefs) {
        if (!symbolsMap.has(ref)) {
            const uuid = `sym-${ref}`;
            const symbolObj = {
                uuid,
                reference: ref,
                value: def.value || '',
                libId: def.package || def.device || '',
                unit: 1,
                footprint: def.package || '',
                properties: {
                    Reference: ref,
                    Value: def.value || '',
                    Footprint: def.package || '',
                    Package: def.package || '',
                    Device: def.device || ''
                },
                at: { x: 0, y: 0, angle: 0 },
                pins: [],
                instancePins: []
            };
            symbolsMap.set(ref, symbolObj);
        }
    }

    // 3. Build Net objects from parsed nets
    for (const net of parsedEasyEDA.nets) {
        const netName = net.name;
        const members = [];

        for (const conn of net.connections) {
            const pinKey = `${conn.component}:${conn.pin}`;
            const pinObj = pinsMap.get(pinKey);

            if (pinObj) {
                members.push({
                    symbolRef: conn.component,
                    pinNumber: conn.pin,
                    pinName: conn.pin,
                    pinUuid: pinObj.uuid
                });
            } else {
                // Pin might not have been created (component with no other connections)
                members.push({
                    symbolRef: conn.component,
                    pinNumber: conn.pin,
                    pinName: conn.pin,
                    pinUuid: `${conn.component}-pin-${conn.pin}`
                });
            }
        }

        netsMap.set(netName, {
            name: netName,
            members
        });
    }

    const symbols = Array.from(symbolsMap.values());
    const nets = Array.from(netsMap.values());

    // 4. Trace function (same as in net_extractor)
    function findTrace(mcuRef, pinIdentifier) {
        const mcuSym = symbolsMap.get(mcuRef);
        if (!mcuSym) return [];

        const mcuPin = mcuSym.pins.find(p => p.number === String(pinIdentifier) || p.name === String(pinIdentifier));
        if (!mcuPin || mcuPin.nets.length === 0) return [];

        const connectedExternalPins = [];
        for (const netName of mcuPin.nets) {
            const net = netsMap.get(netName);
            if (!net) continue;
            for (const member of net.members) {
                if (member.symbolRef !== mcuRef) {
                    const extSym = symbolsMap.get(member.symbolRef);
                    if (extSym) {
                        const extPin = extSym.pins.find(p => p.number === member.pinNumber);
                        connectedExternalPins.push({
                            netName,
                            mcuPin: { reference: mcuRef, number: mcuPin.number, name: mcuPin.name },
                            externalSymbol: { reference: extSym.reference, value: extSym.value, libId: extSym.libId },
                            externalPin: extPin ? { number: extPin.number, name: extPin.name, uuid: extPin.uuid } : null
                        });
                    }
                }
            }
        }
        return connectedExternalPins;
    }

    return {
        symbols,
        pins: allPins,
        nets,
        symbolsMap,
        pinsMap,
        netsMap,
        findTrace
    };
}

/**
 * Full EasyEDA import pipeline: .tel text -> Normalized Schematic Model
 * @param {string} telContent - Raw .tel file content
 * @returns {Object} Normalized model compatible with rest of pipeline
 */
function importEasyEDASchematic(telContent) {
    const rawParsed = parseEasyEDANetlist(telContent);
    const normalizedModel = easyedaToNormalizedModel(rawParsed);
    return {
        rawParsed,
        normalizedModel
    };
}

module.exports = {
    parseEasyEDANetlist,
    easyedaToNormalizedModel,
    importEasyEDASchematic
};
