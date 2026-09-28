// Controller support for the SPECK web page. A plain-script port of Special Battle's input layer
// (special-battle/packages/client/src/input/: PadReader, stick hysteresis, trigger thresholds,
// brand detection, button labels), the same code lopocozo.com's Game Boy uses.
//
// - Every connected pad plays; they're read once per animation frame and presses go to
//   EmulatorJS with simulateInput, held for at least MIN_FRAMES emulated frames like Z and X.
// - EmulatorJS's own gamepad reader is shown no pads (it has no deadzones or trigger
//   thresholds), so nothing is pressed twice. Load this before loader.js.
// - Positional mapping, the same hand position on every brand: bottom face button = A (jump),
//   left or right face button = B (sand), bumpers/triggers = L/R, D-pad or left stick = D-pad.
// - While a pad is what you're playing with, the legend (<kbd data-b="...">) shows its buttons
//   and #pad names it.
(function () {
  var MIN_FRAMES = 3;

  // Standard-mapping indices -> GBA button. First entry = the label shown in the legend.
  var BINDINGS = {
    a: [0], b: [2, 1], l: [4, 6], r: [5, 7], start: [9], select: [8],
    up: [12], down: [13], left: [14], right: [15]
  };
  // EmulatorJS input indices (RetroArch joypad order).
  var INPUT = { b: 0, select: 2, start: 3, up: 4, down: 5, left: 6, right: 7, a: 8, l: 10, r: 11 };
  var BUTTONS = Object.keys(BINDINGS);
  var ANALOG = [6, 7];
  var TRIGGER = { pressAt: 0.4, releaseBelow: 0.2 };
  // A direction starts at 0.35 and lets go below 0.25. Leaving a held 45° sector takes ~8° past its edge.
  var STICK = { deadzone: 0.35, releaseZone: 0.25, margin: (8 * Math.PI) / 180 };
  var SECTOR = Math.PI / 4;
  var ARMS = [['right'], ['right', 'down'], ['down'], ['down', 'left'], ['left'], ['left', 'up'], ['up'], ['up', 'right']];

  function sectorOf(angle) {
    return ((Math.round(angle / SECTOR) % 8) + 8) % 8;
  }
  function angleDiff(a, b) {
    var d = (a - b) % (2 * Math.PI);
    return d > Math.PI ? d - 2 * Math.PI : d < -Math.PI ? d + 2 * Math.PI : d;
  }
  /** 8-way sector (0 = right, clockwise, +y down) with hysteresis; null = centred. */
  function stickSector(x, y, prev) {
    var mag = Math.hypot(x, y);
    if (prev === null) return mag >= STICK.deadzone ? sectorOf(Math.atan2(y, x)) : null;
    if (mag < STICK.releaseZone) return null;
    var angle = Math.atan2(y, x);
    if (Math.abs(angleDiff(angle, prev * SECTOR)) <= SECTOR / 2 + STICK.margin) return prev;
    return sectorOf(angle);
  }

  /** One pad's reader: holds its hysteresis memory (stick sector, trigger state). */
  function PadReader() {
    this.sector = null;
    this.analogDown = {};
  }
  PadReader.prototype.read = function (pad) {
    var self = this;
    var buttons = pad.buttons.map(function (b, i) {
      if (ANALOG.indexOf(i) < 0) return b.pressed;
      // Browsers flag triggers "pressed" at ~12% travel; use our own thresholds.
      var value = b.value > 0 ? b.value : b.pressed ? 1 : 0;
      var on = self.analogDown[i] ? value > TRIGGER.releaseBelow : value >= TRIGGER.pressAt;
      self.analogDown[i] = on;
      return on;
    });
    var down = {};
    BUTTONS.forEach(function (g) {
      if (BINDINGS[g].some(function (i) { return buttons[i]; })) down[g] = true;
    });
    this.sector = stickSector(pad.axes[0] || 0, pad.axes[1] || 0, this.sector);
    if (this.sector !== null) ARMS[this.sector].forEach(function (arm) { down[arm] = true; });
    return down;
  };

  // --- Brands and labels ------------------------------------------------------------------------
  var VENDOR = { 0x045e: 'xbox', 0x054c: 'playstation', 0x057e: 'nintendo' };
  function family(id) {
    var m = /vendor:\s*([0-9a-f]{1,4})/i.exec(id) || /^([0-9a-f]{1,4})-[0-9a-f]{1,4}-/i.exec(id);
    var v = m ? parseInt(m[1], 16) : null;
    if (v !== null && VENDOR[v]) return VENDOR[v];
    var s = id.toLowerCase();
    if (/xbox|xinput|045e/.test(s)) return 'xbox';
    if (/057e|pro controller|joy-con|nintendo|switch/.test(s)) return 'nintendo';
    if (/054c|dualsense|dualshock|wireless controller|playstation/.test(s)) return 'playstation';
    return 'generic';
  }
  var FAMILY_NAME = { xbox: 'Xbox Controller', playstation: 'PlayStation Controller', nintendo: 'Nintendo Controller', generic: 'Controller' };
  function friendlyName(id) {
    var name = id.replace(/\s*\([\s\S]*\)\s*$/, '').replace(/^[0-9a-f]{1,4}-[0-9a-f]{1,4}-/i, '').trim();
    if (!name || /^wireless controller$/i.test(name)) return FAMILY_NAME[family(id)];
    return name;
  }
  var XBOX = { 0: 'A', 1: 'B', 2: 'X', 3: 'Y', 4: 'LB', 5: 'RB', 6: 'LT', 7: 'RT', 8: 'View', 9: 'Menu' };
  var LABELS = {
    xbox: XBOX,
    generic: Object.assign({}, XBOX, { 8: 'Select', 9: 'Start' }),
    playstation: { 0: '✕', 1: '○', 2: '□', 3: '△', 4: 'L1', 5: 'R1', 6: 'L2', 7: 'R2', 8: 'Share', 9: 'Options' },
    // Standard mapping is positional, so the bottom button is Nintendo's "B".
    nintendo: { 0: 'B', 1: 'A', 2: 'Y', 3: 'X', 4: 'L', 5: 'R', 6: 'ZL', 7: 'ZR', 8: '−', 9: '+' }
  };
  var ARROWS = { 12: '↑', 13: '↓', 14: '←', 15: '→' };
  var XBOX_COLORS = { 0: '#6cc24a', 1: '#e8483f', 2: '#3d8ee6', 3: '#f4c431' };
  var FACE_COLORS = { xbox: XBOX_COLORS, generic: XBOX_COLORS, playstation: { 0: '#86aef0', 1: '#f06b70', 2: '#df8fd0', 3: '#43cfa9' } };

  // --- Legend -----------------------------------------------------------------------------------
  var shownFamily = null; // null = keyboard labels
  var shownName = '';
  function showLegend(fam, name) {
    if (fam === shownFamily && name === shownName) return;
    shownFamily = fam;
    shownName = name;
    var caps = document.querySelectorAll('kbd[data-b]');
    for (var i = 0; i < caps.length; i++) {
      var k = caps[i];
      if (!k.hasAttribute('data-key')) k.setAttribute('data-key', k.textContent);
      if (!fam) {
        k.textContent = k.getAttribute('data-key');
        k.style.color = '';
        continue;
      }
      var index = BINDINGS[k.getAttribute('data-b')][0];
      k.textContent = ARROWS[index] || LABELS[fam][index] || 'B' + index;
      k.style.color = (FACE_COLORS[fam] && FACE_COLORS[fam][index]) || '';
    }
    var el = document.getElementById('pad');
    if (el) el.textContent = fam ? name : '';
  }
  // A key press hands the legend back to the keyboard.
  addEventListener('keydown', function () { showLegend(null, ''); }, true);

  // --- Hide pads from EmulatorJS; keep the real reader for us --------------------------------------
  var realPads = typeof navigator.getGamepads === 'function' ? navigator.getGamepads.bind(navigator) : null;
  try {
    Object.defineProperty(navigator, 'getGamepads', { configurable: true, value: function () { return []; } });
  } catch (e) {
    /* can't shadow it: EmulatorJS reads pads too (they press the same buttons) */
  }
  if (!realPads) return;

  // --- The loop ---------------------------------------------------------------------------------
  function gm() {
    try { return window.EJS_emulator && EJS_emulator.gameManager; } catch (e) { return null; }
  }
  var readers = {};  // pad key -> { reader, down }
  var held = {};     // GBA button -> emulated frame it went down on (pad presses only)

  function press(b) {
    var g = gm();
    if (!g || !g.getFrameNum) return; // not running yet
    held[b] = g.getFrameNum();
    g.simulateInput(0, INPUT[b], 1);
  }
  /** Too soon after the press: stays held and is tried again next frame. */
  function release(b) {
    var g = gm();
    if (!g || g.getFrameNum() - held[b] < MIN_FRAMES) return;
    g.simulateInput(0, INPUT[b], 0);
    delete held[b];
  }

  function frame() {
    requestAnimationFrame(frame);
    var pads = [];
    try {
      pads = Array.prototype.filter.call(realPads() || [], function (p) { return p && p.connected; });
    } catch (e) {
      /* permissions policy: no pads */
    }
    var now = {};
    var seen = {};
    var active = null;
    pads.forEach(function (pad) {
      var key = pad.index + ':' + pad.id;
      seen[key] = true;
      var entry = readers[key] || (readers[key] = { reader: new PadReader(), down: {} });
      var down = entry.reader.read(pad);
      for (var b in down) {
        if (!entry.down[b]) active = pad;
        now[b] = true;
      }
      entry.down = down;
    });
    for (var key in readers) if (!seen[key]) delete readers[key];
    if (active) showLegend(family(active.id), friendlyName(active.id));
    else if (shownFamily && pads.length === 0) showLegend(null, '');

    for (var b in held) if (!now[b]) release(b);
    for (var c in now) if (!(c in held)) press(c);
  }
  requestAnimationFrame(frame);
})();
