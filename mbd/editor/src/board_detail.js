// board_detail.js — plain classic browser script, no ES modules, no deps.
// Summarizes and renders a board descriptor (from GET /api/boards) for the
// board-detail panel in the HyprAccel MBD editor.
//
// Exposed as window.HyprBoardDetail = { summarize, render }
// and, under Node, as module.exports = { summarize, render } (for unit tests).

(function (root) {
  'use strict';

  function isPlainObject(v) {
    return v !== null && typeof v === 'object' && !Array.isArray(v);
  }

  function asArray(v) {
    return Array.isArray(v) ? v.slice() : [];
  }

  function toHex(n) {
    if (typeof n === 'number' && isFinite(n)) {
      return '0x' + (n >>> 0).toString(16);
    }
    if (typeof n === 'string') {
      // Already a hex-ish or decimal string; pass through if it already
      // looks like a hex literal, otherwise try to parse it as a number.
      if (/^0x[0-9a-fA-F]+$/.test(n)) return n;
      var parsed = Number(n);
      if (isFinite(parsed)) return '0x' + (parsed >>> 0).toString(16);
      return n;
    }
    return String(n);
  }

  // Build the list of bus entries (spi/uart-style: instance -> {role: pin}).
  function busListSimple(section, roles) {
    var out = [];
    if (!isPlainObject(section)) return out;
    Object.keys(section).forEach(function (instance) {
      var entry = section[instance];
      if (!isPlainObject(entry)) return;
      var pins = {};
      roles.forEach(function (role) {
        if (entry[role] !== undefined) pins[role] = entry[role];
      });
      out.push({ instance: instance, pins: pins });
    });
    return out;
  }

  function i2cList(section) {
    var out = [];
    if (!isPlainObject(section)) return out;
    Object.keys(section).forEach(function (instance) {
      var entry = section[instance];
      if (!isPlainObject(entry)) return;
      var pins = {};
      if (entry.scl !== undefined) pins.scl = entry.scl;
      if (entry.sda !== undefined) pins.sda = entry.sda;

      var devices = [];
      if (isPlainObject(entry.devices)) {
        Object.keys(entry.devices).forEach(function (devName) {
          var dev = entry.devices[devName];
          var address = isPlainObject(dev) ? dev.address : undefined;
          devices.push({ name: devName, address: toHex(address) });
        });
      }

      out.push({ instance: instance, pins: pins, devices: devices });
    });
    return out;
  }

  function summarize(board) {
    if (!board || typeof board !== 'object') return null;

    var pins = isPlainObject(board.pins) ? board.pins : {};

    var gpio = asArray(pins.gpio);
    var pwm = asArray(pins.pwm);
    var adc = asArray(pins.adc);

    var spiBuses = busListSimple(pins.spi, ['sck', 'mosi', 'miso', 'cs']);
    var uartBuses = busListSimple(pins.uart, ['tx', 'rx']);
    var i2cBuses = i2cList(pins.i2c);

    return {
      name: board.name != null ? board.name : '',
      architecture: board.architecture != null ? board.architecture : '',
      mcu: board.mcu != null ? board.mcu : '',
      clockMhz: board.clock_freq_mhz != null ? board.clock_freq_mhz : null,
      fpgaTransport: board.fpga_transport != null ? board.fpga_transport : '',
      accelerators: asArray(board.accelerators),
      counts: {
        gpio: gpio.length,
        spi: spiBuses.length,
        i2c: i2cBuses.length,
        uart: uartBuses.length,
        pwm: pwm.length,
        adc: adc.length
      },
      buses: {
        spi: spiBuses,
        i2c: i2cBuses,
        uart: uartBuses
      },
      pwm: pwm,
      adc: adc
    };
  }

  // ---------------------------------------------------------------------
  // Rendering (browser only — guarded so require()'ing this file in Node
  // for tests never touches `document`).
  // ---------------------------------------------------------------------

  var STYLE_ID = 'bd-styles';
  var STYLE_CSS = [
    '.bd-root{font-family:inherit;color:var(--text);font-size:12px;',
    'max-width:560px;}',
    '.bd-placeholder{color:var(--text-muted);font-size:12px;padding:16px 4px;}',
    '.bd-header{display:flex;flex-direction:column;gap:2px;margin-bottom:8px;}',
    '.bd-name{font-weight:600;font-size:13px;color:var(--text);}',
    '.bd-meta{color:var(--text-muted);font-size:11px;font-family:var(--font-mono);}',
    '.bd-accels{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0;}',
    '.bd-chip{display:inline-flex;align-items:center;gap:4px;padding:2px 8px;',
    'border-radius:999px;background:var(--accent-glow);color:var(--accent);',
    'font-size:11px;font-family:var(--font-mono);border:1px solid var(--border);}',
    '.bd-chip-transport{color:var(--purple);}',
    '.bd-counts{display:flex;flex-wrap:wrap;gap:10px;margin:8px 0;',
    'padding:6px 8px;background:var(--panel-header);border:1px solid var(--border);',
    'border-radius:6px;}',
    '.bd-count{display:flex;flex-direction:column;align-items:center;min-width:34px;}',
    '.bd-count-val{font-family:var(--font-mono);font-weight:600;color:var(--green);',
    'font-size:12px;}',
    '.bd-count-label{color:var(--text-muted);font-size:9px;text-transform:uppercase;',
    'letter-spacing:.04em;}',
    '.bd-pinmap{margin-top:8px;border:1px solid var(--border);border-radius:6px;',
    'background:var(--panel);}',
    '.bd-pinmap-summary{cursor:pointer;padding:6px 8px;color:var(--text-muted);',
    'font-size:11px;user-select:none;}',
    '.bd-pinmap-summary:hover{color:var(--text);}',
    '.bd-pinmap-body{max-height:180px;overflow-y:auto;padding:6px 8px;',
    'border-top:1px solid var(--border);}',
    '.bd-bus-block{margin-bottom:8px;}',
    '.bd-bus-title{font-family:var(--font-mono);font-size:11px;color:var(--accent);',
    'margin-bottom:2px;}',
    '.bd-role-row{display:flex;justify-content:space-between;gap:8px;',
    'font-family:var(--font-mono);font-size:11px;color:var(--text);padding:1px 0;}',
    '.bd-role-name{color:var(--text-muted);}',
    '.bd-device-row{display:flex;justify-content:space-between;gap:8px;',
    'font-family:var(--font-mono);font-size:10px;color:var(--purple);padding:1px 0 1px 10px;}',
    '.bd-pinlist{font-family:var(--font-mono);font-size:11px;color:var(--text);',
    'word-break:break-word;}',
    '.bd-pinlist-title{color:var(--accent);margin-bottom:2px;font-size:11px;}',
    '.bd-empty{color:var(--text-muted);font-style:italic;}'
  ].join('');

  function ensureStyles(doc) {
    if (doc.getElementById(STYLE_ID)) return;
    var style = doc.createElement('style');
    style.id = STYLE_ID;
    style.textContent = STYLE_CSS;
    doc.head.appendChild(style);
  }

  function el(doc, tag, className, text) {
    var node = doc.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = String(text);
    return node;
  }

  function renderPlaceholder(doc, container) {
    var p = el(doc, 'div', 'bd-placeholder', 'Select a target board');
    container.appendChild(p);
  }

  function renderCounts(doc, counts) {
    var wrap = el(doc, 'div', 'bd-counts');
    var order = [
      ['gpio', 'GPIO'],
      ['spi', 'SPI'],
      ['i2c', 'I2C'],
      ['uart', 'UART'],
      ['pwm', 'PWM'],
      ['adc', 'ADC']
    ];
    order.forEach(function (pair) {
      var key = pair[0];
      var label = pair[1];
      var item = el(doc, 'div', 'bd-count');
      item.appendChild(el(doc, 'span', 'bd-count-val', counts[key]));
      item.appendChild(el(doc, 'span', 'bd-count-label', label));
      wrap.appendChild(item);
    });
    return wrap;
  }

  function renderBusBlock(doc, bus, roleOrder) {
    var block = el(doc, 'div', 'bd-bus-block');
    block.appendChild(el(doc, 'div', 'bd-bus-title', bus.instance));
    roleOrder.forEach(function (role) {
      if (bus.pins[role] === undefined) return;
      var row = el(doc, 'div', 'bd-role-row');
      row.appendChild(el(doc, 'span', 'bd-role-name', role));
      row.appendChild(el(doc, 'span', null, bus.pins[role]));
      block.appendChild(row);
    });
    if (bus.devices && bus.devices.length) {
      bus.devices.forEach(function (dev) {
        var row = el(doc, 'div', 'bd-device-row');
        row.appendChild(el(doc, 'span', null, dev.name));
        row.appendChild(el(doc, 'span', null, dev.address));
        block.appendChild(row);
      });
    }
    return block;
  }

  function renderPinList(doc, title, pins) {
    var wrap = el(doc, 'div', 'bd-pinlist');
    wrap.appendChild(el(doc, 'div', 'bd-pinlist-title', title));
    if (!pins.length) {
      wrap.appendChild(el(doc, 'span', 'bd-empty', 'none'));
    } else {
      wrap.appendChild(doc.createTextNode(pins.join(', ')));
    }
    return wrap;
  }

  function renderPinMap(doc, summary) {
    var details = doc.createElement('details');
    details.className = 'bd-pinmap';
    var sum = el(doc, 'summary', 'bd-pinmap-summary', 'Pin map');
    details.appendChild(sum);

    var body = el(doc, 'div', 'bd-pinmap-body');

    summary.buses.spi.forEach(function (bus) {
      body.appendChild(renderBusBlock(doc, bus, ['sck', 'mosi', 'miso', 'cs']));
    });
    summary.buses.i2c.forEach(function (bus) {
      body.appendChild(renderBusBlock(doc, bus, ['scl', 'sda']));
    });
    summary.buses.uart.forEach(function (bus) {
      body.appendChild(renderBusBlock(doc, bus, ['tx', 'rx']));
    });

    body.appendChild(renderPinList(doc, 'PWM', summary.pwm));
    body.appendChild(renderPinList(doc, 'ADC', summary.adc));

    details.appendChild(body);
    return details;
  }

  function render(container, board) {
    if (!container) return;
    var doc = container.ownerDocument || (typeof document !== 'undefined' ? document : null);
    if (!doc) return;

    ensureStyles(doc);

    while (container.firstChild) {
      container.removeChild(container.firstChild);
    }

    var root = el(doc, 'div', 'bd-root');
    container.appendChild(root);

    var summary = summarize(board);
    if (!summary) {
      renderPlaceholder(doc, root);
      return;
    }

    var header = el(doc, 'div', 'bd-header');
    header.appendChild(el(doc, 'div', 'bd-name', summary.name));
    var metaParts = [summary.architecture];
    if (summary.clockMhz != null) metaParts.push(summary.clockMhz + ' MHz');
    if (summary.mcu) metaParts.push(summary.mcu);
    header.appendChild(el(doc, 'div', 'bd-meta', metaParts.filter(Boolean).join(' · ')));
    root.appendChild(header);

    if (summary.accelerators.length) {
      var accels = el(doc, 'div', 'bd-accels');
      summary.accelerators.forEach(function (name) {
        var chip = el(doc, 'span', 'bd-chip', name);
        if (summary.fpgaTransport) {
          chip.appendChild(el(doc, 'span', 'bd-chip-transport', ' via ' + summary.fpgaTransport));
        }
        accels.appendChild(chip);
      });
      root.appendChild(accels);
    }

    root.appendChild(renderCounts(doc, summary.counts));
    root.appendChild(renderPinMap(doc, summary));
  }

  var api = { summarize: summarize, render: render };

  if (typeof window !== 'undefined') {
    window.HyprBoardDetail = api;
  }
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = api;
  }
})(typeof window !== 'undefined' ? window : this);
