/**
 * net_extractor.js — Normalized Symbol and Net Extraction Engine (SCH-T2)
 *
 * Transforms raw parsed schematic AST into a normalized internal graph
 * representation: Symbol, Pin, Net, preserving MCU pin -> net -> component pin traceability.
 */

/**
 * Normalizes symbols, pins, and nets from parsed schematic.
 * @param {Object} parsedSch - Output of parseKiCadSchematic()
 * @returns {Object} Normalized model { symbols, pins, nets, findTrace }
 */
function extractNormalizedSchematic(parsedSch) {
    if (!parsedSch || typeof parsedSch !== 'object') {
        throw new Error('Invalid input: parsed schematic object required.');
    }

    const symbolsMap = new Map();
    const allPins = [];
    const pinsMap = new Map(); // key: uuid or `${symbolRef}:${number}`

    // 1. Build Symbols and Pins
    for (const rawSym of parsedSch.symbols || []) {
        const reference = rawSym.reference || rawSym.properties['Reference'] || 'U?';
        const value = rawSym.value || rawSym.properties['Value'] || '';
        const uuid = rawSym.uuid || `sym-${Math.random().toString(36).substr(2, 9)}`;

        const symbolObj = {
            uuid,
            reference,
            value,
            libId: rawSym.libId || '',
            unit: rawSym.unit || 1,
            footprint: rawSym.footprint || '',
            properties: { ...rawSym.properties },
            at: { ...rawSym.at },
            pins: []
        };

        // Gather pin details from libDef or instance
        const libPinsByNum = new Map();
        if (rawSym.libDef && Array.isArray(rawSym.libDef.pins)) {
            for (const p of rawSym.libDef.pins) {
                libPinsByNum.set(String(p.number), p);
            }
        }

        const instancePins = rawSym.instancePins && rawSym.instancePins.length > 0
            ? rawSym.instancePins
            : (rawSym.libDef ? rawSym.libDef.pins : []);

        for (const instPin of instancePins) {
            const pNum = String(instPin.number || '');
            const libPin = libPinsByNum.get(pNum);
            const pName = (libPin && libPin.name) ? libPin.name : (instPin.name || pNum);
            const pUuid = instPin.uuid || `${uuid}-pin-${pNum}`;

            // Calculate approximate absolute pin position
            const pRelAt = (libPin && libPin.at) ? libPin.at : (instPin.at || { x: 0, y: 0 });
            const absX = Math.round((rawSym.at.x + (pRelAt.x || 0)) * 1000) / 1000;
            const absY = Math.round((rawSym.at.y + (pRelAt.y || 0)) * 1000) / 1000;

            const pinObj = {
                number: pNum,
                name: pName,
                uuid: pUuid,
                symbolRef: reference,
                symbolUuid: uuid,
                position: { x: absX, y: absY },
                nets: []
            };

            symbolObj.pins.push(pinObj);
            allPins.push(pinObj);
            pinsMap.set(pUuid, pinObj);
            pinsMap.set(`${reference}:${pNum}`, pinObj);
        }

        symbolsMap.set(reference, symbolObj);
    }

    // 2. Connectivity Graph Building (Coordinate Matching)
    // Map coordinate string "x,y" -> Set of attached items
    const coordMap = new Map();

    function keyAt(x, y) {
        const rx = Math.round(x * 100) / 100;
        const ry = Math.round(y * 100) / 100;
        return `${rx},${ry}`;
    }

    function addCoordItem(x, y, item) {
        const k = keyAt(x, y);
        if (!coordMap.has(k)) {
            coordMap.set(k, []);
        }
        coordMap.get(k).push(item);
    }

    // Add pin positions
    for (const pin of allPins) {
        addCoordItem(pin.position.x, pin.position.y, { type: 'pin', pin });
    }

    // Add wire endpoints & segments
    const adj = new Map(); // adjacency list for wire nodes
    function addAdjEdge(k1, k2) {
        if (!adj.has(k1)) adj.set(k1, new Set());
        if (!adj.has(k2)) adj.set(k2, new Set());
        adj.get(k1).add(k2);
        adj.get(k2).add(k1);
    }

    for (const wire of parsedSch.wires || []) {
        if (!wire.pts || wire.pts.length < 2) continue;
        for (let i = 0; i < wire.pts.length - 1; i++) {
            const k1 = keyAt(wire.pts[i].x, wire.pts[i].y);
            const k2 = keyAt(wire.pts[i + 1].x, wire.pts[i + 1].y);
            addAdjEdge(k1, k2);
        }
    }

    // Add labels at coordinates
    const labelCoords = new Map();
    for (const lbl of parsedSch.labels || []) {
        const k = keyAt(lbl.at.x, lbl.at.y);
        if (!labelCoords.has(k)) labelCoords.set(k, []);
        labelCoords.get(k).push(lbl);
    }

    // 3. Find Connected Components (Nets)
    const visitedCoords = new Set();
    const netsMap = new Map();
    let autoNetId = 1;

    // Helper: traverse component from starting coord key
    function getComponentCoords(startKey) {
        const comp = [];
        const queue = [startKey];
        visitedCoords.add(startKey);

        while (queue.length > 0) {
            const curr = queue.shift();
            comp.push(curr);
            const neighbors = adj.get(curr) || [];
            for (const nxt of neighbors) {
                if (!visitedCoords.has(nxt)) {
                    visitedCoords.add(nxt);
                    queue.push(nxt);
                }
            }
        }
        return comp;
    }

    // Gather all candidate starting keys (wire keys, pin keys, label keys)
    const allCoordKeys = new Set([
        ...coordMap.keys(),
        ...adj.keys(),
        ...labelCoords.keys()
    ]);

    for (const startKey of allCoordKeys) {
        if (visitedCoords.has(startKey)) continue;

        const compKeys = getComponentCoords(startKey);
        const compPins = [];
        const compLabels = [];

        for (const k of compKeys) {
            const items = coordMap.get(k) || [];
            for (const item of items) {
                if (item.type === 'pin') compPins.push(item.pin);
            }
            const lbls = labelCoords.get(k) || [];
            compLabels.push(...lbls);
        }

        // Determine net name
        let netName = '';

        // Priority 1: Global / Hierarchical / Local labels
        if (compLabels.length > 0) {
            // Sort: global > hierarchical > local
            compLabels.sort((a, b) => {
                const rank = { global_label: 3, hierarchical_label: 2, label: 1 };
                return (rank[b.type] || 0) - (rank[a.type] || 0);
            });
            netName = compLabels[0].name;
        }

        // Priority 2: Auto net name from member pin
        if (!netName && compPins.length > 0) {
            const p0 = compPins[0];
            netName = `Net-(${p0.symbolRef}-${p0.name || p0.number})`;
        }

        // Priority 3: Fallback auto net
        if (!netName) {
            netName = `Net-N${autoNetId++}`;
        }

        // Create Net object
        if (!netsMap.has(netName)) {
            netsMap.set(netName, {
                name: netName,
                members: []
            });
        }
        const netObj = netsMap.get(netName);

        for (const pin of compPins) {
            if (!pin.nets.includes(netName)) {
                pin.nets.push(netName);
            }
            const memberRecord = {
                symbolRef: pin.symbolRef,
                pinNumber: pin.number,
                pinName: pin.name,
                pinUuid: pin.uuid
            };
            // Avoid duplicate member records
            if (!netObj.members.some(m => m.symbolRef === pin.symbolRef && m.pinNumber === pin.number)) {
                netObj.members.push(memberRecord);
            }
        }
    }

    // Handle pins that were not connected to any wire network
    for (const pin of allPins) {
        if (pin.nets.length === 0) {
            const standaloneNetName = `Net-(${pin.symbolRef}-${pin.name || pin.number})`;
            pin.nets.push(standaloneNetName);
            if (!netsMap.has(standaloneNetName)) {
                netsMap.set(standaloneNetName, {
                    name: standaloneNetName,
                    members: [{
                        symbolRef: pin.symbolRef,
                        pinNumber: pin.number,
                        pinName: pin.name,
                        pinUuid: pin.uuid
                    }]
                });
            }
        }
    }

    const symbols = Array.from(symbolsMap.values());
    const nets = Array.from(netsMap.values());

    /**
     * Trace MCU pin to external component pins connected via nets.
     * @param {string} mcuRef - Reference designator of MCU (e.g. "U1")
     * @param {string} pinIdentifier - Pin number or pin name (e.g. "13" or "GPIO14")
     * @returns {Array<Object>} List of connected external component pins
     */
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

module.exports = {
    extractNormalizedSchematic
};
