#include "web_assets.h"

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sprinky</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<header>
  <h1>🌱 <span id="hostname">Sprinky</span></h1>
  <span id="conn" class="pill offline">offline</span>
</header>
<svg class="wave-divider" viewBox="0 0 1440 40" preserveAspectRatio="none" aria-hidden="true">
  <path d="M0,20 C240,40 480,0 720,15 C960,30 1200,5 1440,20 L1440,40 L0,40 Z" fill="var(--surface-100)"></path>
</svg>

<nav class="tabs">
  <button class="tab-btn active" data-tab="status">Status</button>
  <button class="tab-btn" data-tab="valves">Valves</button>
  <button class="tab-btn" data-tab="setup">Setup</button>
</nav>

<main>
  <section id="tab-status" class="tab active">
    <div class="grid">
      <div class="card">
        <div class="card-label">⏱️ Time</div>
        <div id="time" class="card-value">--:--:--</div>
        <div id="date" class="card-sub"></div>
        <div id="tz" class="card-sub"></div>
      </div>
      <div class="card">
        <div class="card-label">🌡️ Outside Temp</div>
        <div id="temp" class="card-value">-- °F</div>
        <div id="avgtemp" class="card-sub"></div>
        <button id="temp-unit" type="button" class="btn small">°F / °C</button>
      </div>
      <div class="card">
        <div class="card-label">📶 WiFi Signal</div>
        <div id="rssi" class="card-value">-- dBm</div>
        <div id="ip" class="card-sub"></div>
      </div>
      <div class="card">
        <div class="card-label">💧 Last Run</div>
        <div id="runtime" class="card-value">--</div>
      </div>
    </div>

    <div class="card wide">
      <div class="switch-row">
        <div>
          <div class="card-label">🚿 Watering</div>
          <div id="water-state" class="card-sub"></div>
        </div>
        <label class="switch">
          <input type="checkbox" id="water-toggle">
          <span class="slider"></span>
        </label>
      </div>
    </div>

    <div class="card wide">
      <div class="card-label">📜 Log</div>
      <pre id="log"></pre>
    </div>
  </section>

  <section id="tab-valves" class="tab">
    <div class="card wide">
      <div class="card-label">🚰 Diagnostics — open a valve for one minute</div>
      <div id="valve-buttons" class="valve-buttons"></div>
    </div>

    <div class="card wide">
      <div class="card-label">📅 Schedule</div>
      <div class="card-sub" style="margin-bottom: 0.5rem">24-hour time &mdash; e.g. 19 = 7 PM</div>
      <div class="form-row">
        <label for="run-hour">Run hour</label>
        <input type="number" id="run-hour" min="0" max="23">
        <label for="run-minute">Run minute</label>
        <input type="number" id="run-minute" min="0" max="59">
        <span id="run-time-12h" class="card-sub"></span>
      </div>
      <div class="card-label">Days to Water</div>
      <div id="day-checkboxes" class="day-checkboxes"></div>

      <div class="form-row">
        <label for="season">Season profile</label>
        <select id="season">
          <option value="0">Summer</option>
          <option value="1">Fall</option>
          <option value="2">Winter</option>
          <option value="3">Spring</option>
        </select>
      </div>
      <div class="btn-row">
        <button id="save-schedule" class="btn primary">Save schedule</button>
        <span id="save-schedule-status" class="save-status"></span>
      </div>
      <div class="card-sub" style="margin-bottom: 0.5rem">Saves run time, days, and valve names/run times below, all to the selected season profile.</div>

      <button id="run-now" class="btn">Run Watering Sequence Now</button>
    </div>

    <div class="card wide">
      <div class="card-label">🌿 Valve Names &amp; Run Times</div>
      <div id="valve-config"></div>
      <div id="valve-total" class="valve-total"></div>

      <div class="btn-row">
        <button id="temp-scaling" type="button" class="btn">Temperature scaling: --</button>
      </div>
    </div>
  </section>

  <section id="tab-setup" class="tab">
    <div class="card wide">
      <div class="card-label">📡 WiFi Credentials</div>
      <div class="form-row">
        <label for="wifi-ssid">SSID</label>
        <input type="text" id="wifi-ssid" maxlength="32">
      </div>
      <div class="form-row">
        <label for="wifi-pass">Password</label>
        <input type="password" id="wifi-pass" maxlength="64">
      </div>
      <button id="save-wifi" class="btn primary">Save</button>
      <span id="save-wifi-status" class="save-status"></span>
    </div>

    <div class="card wide">
      <div class="card-label">🕒 Time Zone</div>
      <div class="form-row">
        <select id="timezone">
          <option value="AEST">Australia Eastern</option>
          <option value="MSK">Moscow</option>
          <option value="CE">Central European</option>
          <option value="GMT">GMT/British</option>
          <option value="UTC">Universal (UTC)</option>
          <option value="EST">US Eastern</option>
          <option value="CST">US Central</option>
          <option value="MST">US Mountain</option>
          <option value="AZT">Arizona</option>
          <option value="PST">US Pacific</option>
          <option value="BRT">Brazil (S&atilde;o Paulo)</option>
          <option value="SAST">South Africa</option>
          <option value="GST">Gulf (Dubai)</option>
          <option value="IST">India</option>
          <option value="CNST">China</option>
          <option value="JST">Japan</option>
        </select>
      </div>
    </div>

    <div class="card wide">
      <div class="card-label">🛠️ Maintenance</div>
      <div class="form-row">
        <a href="/update" class="btn">Firmware Update</a>
        <button id="reboot" class="btn danger">Reboot</button>
      </div>
    </div>
  </section>
</main>

<script src="/app.js"></script>
</body>
</html>
)HTML";

const char STYLE_CSS[] PROGMEM = R"CSS(
:root {
  --surface-100: #eef8f7;
  --surface-000: #ffffff;
  --surface-300: #dcece9;
  --border: #d7e9e6;
  --ink: #123b38;
  --ink-secondary: #4f706b;
  --header-from: #0f3d3a;
  --header-to: #0d9488;
  --header-text: #f4fbfa;
  --aqua-500: #14b8a6;
  /* teal-600: the design system's calibrated primary-action color -- 8.6:1
     contrast with --btn-primary-text, unlike a lighter fill. */
  --btn-primary-bg: #1c5049;
  --btn-primary-text: var(--header-text);
  --coral-100: #f7e2da;
  --coral-700: #6e3019;
  --success-100: #dcf3e4;
  --success-700: #153f29;
  --shadow-sm: 0 1px 2px rgba(15,64,60,0.08), 0 1px 3px rgba(15,64,60,0.10);
  --shadow-md: 0 10px 24px rgba(13,148,136,0.20), 0 3px 8px rgba(15,64,60,0.12);
  --font-display: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  --space-1: 4px; --space-2: 8px; --space-3: 12px; --space-4: 16px;
  --space-5: 24px; --space-6: 32px;
  --radius-sm: 8px; --radius-md: 12px; --radius-lg: 20px; --radius-full: 999px;
  --transition-fast: 0.15s;
}
@media (prefers-color-scheme: dark) {
  :root {
    --surface-100: #0b1f1d;
    --surface-000: #123330;
    --surface-300: #1c3d38;
    --border: #24504a;
    --ink: #eaf6f4;
    --ink-secondary: #a7c9c4;
    --header-from: #0a2a27;
    --header-to: #0d9488;
    --header-text: #f4fbfa;
    --aqua-500: #2dd4bf;
    --btn-primary-bg: #3ea996;
    --btn-primary-text: var(--header-text);
    --coral-100: #3e2418;
    --coral-700: #f0b79b;
    --success-100: #163c26;
    --success-700: #bdeed2;
    --shadow-sm: 0 1px 2px rgba(0,0,0,0.35), 0 1px 3px rgba(0,0,0,0.45);
    --shadow-md: 0 10px 26px rgba(0,0,0,0.5), 0 3px 10px rgba(0,0,0,0.35);
  }
}
* { box-sizing: border-box; }
body {
  margin: 0;
  font-family: var(--font-sans);
  background: var(--surface-100);
  color: var(--ink);
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: var(--space-4) var(--space-5);
  background: linear-gradient(135deg, var(--header-from), var(--header-to));
  color: var(--header-text);
}
header h1 {
  font-family: var(--font-display);
  font-size: 22px;
  line-height: 28px;
  font-weight: 700;
  margin: 0;
  color: var(--header-text);
}
.wave-divider { display: block; width: 100%; height: 22px; margin-top: -1px; }
.pill {
  font-size: 13px;
  line-height: 18px;
  font-weight: 500;
  padding: 4px 12px;
  border-radius: var(--radius-full);
}
.pill.online { background: var(--success-100); color: var(--success-700); }
.pill.offline { background: var(--coral-100); color: var(--coral-700); }

.tabs {
  display: flex;
  gap: var(--space-2);
  padding: var(--space-3) var(--space-4);
}
.tab-btn {
  background: none;
  border: none;
  color: var(--ink-secondary);
  font-size: 15px;
  font-weight: 600;
  padding: 10px 16px;
  border-radius: var(--radius-full);
  cursor: pointer;
  transition: background var(--transition-fast), color var(--transition-fast);
}
.tab-btn.active {
  color: var(--btn-primary-text);
  background: var(--btn-primary-bg);
  box-shadow: var(--shadow-sm);
}
main { padding: var(--space-4); max-width: 720px; margin: 0 auto; }
.tab { display: none; }
.tab.active { display: block; }

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
  gap: var(--space-4);
  margin-bottom: var(--space-4);
}
.card {
  background: var(--surface-000);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-sm);
  padding: var(--space-5);
  transition: box-shadow var(--transition-fast), transform var(--transition-fast);
}
.card:hover { box-shadow: var(--shadow-md); transform: translateY(-2px); }
.card.wide { margin-bottom: var(--space-4); }
.card-label {
  font-size: 12px;
  line-height: 16px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--ink-secondary);
  margin-bottom: var(--space-2);
}
.card-value {
  font-family: var(--font-display);
  font-size: clamp(20px, 6vw, 26px);
  line-height: 1.25;
  font-weight: 700;
  color: var(--ink);
}
.card-sub { font-size: 13px; line-height: 18px; font-weight: 500; color: var(--ink-secondary); margin-top: var(--space-1); }

.switch-row { display: flex; align-items: center; justify-content: space-between; }
.switch { position: relative; display: inline-block; width: 48px; height: 28px; }
.switch input { opacity: 0; width: 0; height: 0; }
.switch .slider {
  position: absolute; cursor: pointer; inset: 0;
  background: var(--surface-300); border-radius: var(--radius-full); transition: var(--transition-fast);
}
.switch .slider::before {
  content: ""; position: absolute; height: 22px; width: 22px; left: 3px; bottom: 3px;
  background: var(--surface-000); border-radius: 50%; transition: var(--transition-fast);
  box-shadow: var(--shadow-sm);
}
.switch input:checked + .slider { background: var(--btn-primary-bg); }
.switch input:checked + .slider::before { transform: translateX(20px); }

pre#log {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 12px;
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 200px;
  overflow-y: auto;
  margin: 0;
  color: var(--ink-secondary);
}

.valve-buttons {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
  gap: var(--space-3);
}
.valve-btn {
  border: 1px solid var(--border);
  background: var(--surface-000);
  color: var(--ink);
  border-radius: var(--radius-md);
  padding: var(--space-4) var(--space-2);
  font-size: 15px;
  font-weight: 600;
  cursor: pointer;
  box-shadow: var(--shadow-sm);
  transition: box-shadow var(--transition-fast), transform var(--transition-fast), background var(--transition-fast), color var(--transition-fast);
}
.valve-btn:hover { box-shadow: var(--shadow-md); transform: translateY(-1px); }
.valve-btn.active {
  background: var(--btn-primary-bg);
  color: var(--btn-primary-text);
  border-color: var(--btn-primary-bg);
}

.form-row {
  display: flex;
  align-items: center;
  gap: var(--space-2);
  flex-wrap: wrap;
  margin-bottom: var(--space-3);
}
.form-row label { font-size: 13px; font-weight: 500; color: var(--ink-secondary); min-width: 4.5rem; }
.form-row input[type="text"],
.form-row input[type="password"],
.form-row input[type="number"],
.form-row select {
  flex: 1;
  min-width: 6rem;
  padding: 10px 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--surface-000);
  color: var(--ink);
  font-size: 15px;
}
.form-row input[type="number"] { max-width: 5rem; flex: none; }
.form-row input[type="range"] { flex: 1; accent-color: var(--btn-primary-bg); }

/* A button on its own line with breathing room above it. */
.btn-row { margin-top: var(--space-3); display: flex; align-items: center; flex-wrap: wrap; gap: var(--space-2); }
.btn-row .save-status { margin-left: 0; }

.day-checkboxes {
  display: flex;
  flex-wrap: wrap;
  gap: var(--space-2);
  margin-bottom: var(--space-4);
}
.day-btn {
  border: none;
  background: var(--surface-300);
  color: var(--ink-secondary);
  border-radius: var(--radius-full);
  padding: 8px 14px;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: box-shadow var(--transition-fast), background var(--transition-fast), color var(--transition-fast);
}
.day-btn.active {
  background: var(--btn-primary-bg);
  color: var(--btn-primary-text);
  box-shadow: var(--shadow-sm);
}

.valve-row {
  display: flex;
  align-items: center;
  gap: var(--space-3);
  padding: var(--space-2) 0;
  border-bottom: 1px solid var(--border);
}
.valve-row:last-child { border-bottom: none; }
.valve-name-wrap { position: relative; display: inline-flex; align-items: center; }
.valve-name-wrap::before {
  content: "\270E";  /* pencil: hints the field is editable */
  position: absolute;
  left: 0.55rem;
  font-size: 0.8rem;
  color: var(--ink-secondary);
  pointer-events: none;
}
.valve-row input[type="text"] {
  width: 9rem;
  padding: 10px 10px 10px 1.6rem;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--surface-000);
  color: var(--ink);
  font-size: 13px;
  box-shadow: inset 0 -1px 0 var(--ink-secondary);
}
.valve-row input[type="text"]::placeholder { color: var(--ink-secondary); opacity: 1; }
.valve-row input[type="text"]:focus {
  outline: none;
  border-color: var(--aqua-500);
  box-shadow: 0 0 0 2px var(--aqua-500);
}
.valve-row .runtime-val { min-width: 3.5rem; text-align: right; font-variant-numeric: tabular-nums; font-size: 13px; color: var(--ink-secondary); }

.btn {
  display: inline-block;
  background: var(--surface-000);
  color: var(--ink);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 10px 20px;
  font-size: 15px;
  font-weight: 600;
  cursor: pointer;
  text-decoration: none;
  box-shadow: var(--shadow-sm);
  transition: box-shadow var(--transition-fast), transform var(--transition-fast);
}
.btn:hover { box-shadow: var(--shadow-md); transform: translateY(-1px); }
.btn:active { box-shadow: var(--shadow-sm); transform: translateY(0); }
.btn.primary { background: var(--btn-primary-bg); color: var(--btn-primary-text); border-color: var(--btn-primary-bg); }
.btn.danger { background: var(--coral-100); color: var(--coral-700); border-color: var(--coral-100); }
.btn.small { padding: 6px 12px; font-size: 13px; margin-top: var(--space-2); }
.save-status { font-size: 13px; font-weight: 500; color: var(--ink-secondary); margin-left: var(--space-2); }
.valve-total { font-size: 13px; font-weight: 500; color: var(--ink-secondary); margin: var(--space-2) 0 var(--space-3); }
)CSS";

const char APP_JS[] PROGMEM = R"JS(
(() => {
  let numValves = 0;
  let configBuilt = false;
  let activeDays = 0x7F;
  let curSeason = 0;  // active season profile shown in the Valves tab
  const DAY_NAMES = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

  // Outside-temp display unit. The API always reports Fahrenheit; the toggle
  // button converts client-side. lastTempF/lastAvgTempF hold the most recent
  // reading so a unit switch can re-render without waiting for the next poll.
  let useCelsius = false;
  let lastTempF = null;
  let lastAvgTempF = null;

  // Latest temperature-scaling on/off state from /api/status, for the toggle button.
  let tempScaling = true;

  const $ = (id) => document.getElementById(id);

  $('temp-unit').addEventListener('click', () => {
    useCelsius = !useCelsius;
    renderTemp();
  });

  // Show the 24-hour Run hour/minute fields as a 12-hour time too, so an
  // evening schedule entered as e.g. "7" doesn't silently run at 7 AM.
  function renderRunTime12h() {
    const h = parseInt($('run-hour').value, 10);
    const m = parseInt($('run-minute').value, 10);
    if (isNaN(h) || isNaN(m)) { $('run-time-12h').textContent = ''; return; }
    const period = h < 12 ? 'AM' : 'PM';
    const h12 = ((h % 12) || 12);
    $('run-time-12h').textContent = '= ' + h12 + ':' + String(m).padStart(2, '0') + ' ' + period;
  }
  $('run-hour').addEventListener('input', renderRunTime12h);
  $('run-minute').addEventListener('input', renderRunTime12h);

  // Redraw the Outside Temp card from the last reading, in the selected unit.
  function renderTemp() {
    if (lastTempF === null) return;
    const unit = useCelsius ? ' °C' : ' °F';
    const cur = useCelsius ? (lastTempF - 32) * 5 / 9 : lastTempF;
    const avg = useCelsius ? (lastAvgTempF - 32) * 5 / 9 : lastAvgTempF;
    $('temp').textContent = Math.round(cur) + unit;
    $('avgtemp').textContent = '24h avg: ' + avg.toFixed(1) + unit;
  }

  document.querySelectorAll('.tab-btn').forEach((btn) => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.tab-btn').forEach((b) => b.classList.remove('active'));
      document.querySelectorAll('.tab').forEach((t) => t.classList.remove('active'));
      btn.classList.add('active');
      $('tab-' + btn.dataset.tab).classList.add('active');
    });
  });

  // The valve run-time sliders are shown to the user in fractional minutes to
  // make setting them more natural; everything else (API, state, schedule)
  // stays in whole seconds. secToMin()/minToSec() convert at that boundary.
  function secToMin(sec) { return sec / 60; }
  function minToSec(min) { return Math.round(min * 60); }
  function fmtMinutes(min) {
    // up to two decimals, but drop trailing zeros and a bare decimal point:
    // 5 -> "5 min", 2.5 -> "2.5 min", 0.25 -> "0.25 min"
    const s = (Math.round(min * 100) / 100).toFixed(2).replace(/\.?0+$/, '');
    return (s === '' ? '0' : s) + ' min';
  }

  // Sum every valve slider (values are in minutes) and show the running total
  // just above the Save button. Called on initial render and on every drag.
  function updateValveTotal() {
    let total = 0;
    for (let i = 0; i < numValves; i++) {
      total += parseFloat($('valve-slider-' + i).value);
    }
    $('valve-total').textContent = 'Total watering time: ' + fmtMinutes(total);
  }

  function buildValveUI(valves) {
    numValves = valves.length;
    const buttonsEl = $('valve-buttons');
    const configEl = $('valve-config');
    buttonsEl.innerHTML = '';
    configEl.innerHTML = '';

    valves.forEach((v, i) => {
      const btn = document.createElement('button');
      btn.className = 'valve-btn';
      btn.id = 'valve-btn-' + i;
      btn.textContent = v.name;
      btn.addEventListener('click', () => toggleValve(i, !btn.classList.contains('active')));
      buttonsEl.appendChild(btn);

      const row = document.createElement('div');
      row.className = 'valve-row';
      row.innerHTML =
        '<span class="valve-name-wrap">' +
          '<input type="text" id="valve-name-' + i + '" value="" maxlength="14" ' +
          'placeholder="Valve ' + (i + 1) + ' name" ' +
          'aria-label="Name for valve ' + (i + 1) + '" ' +
          'title="Tap to rename this valve"></span>' +
        '<input type="range" id="valve-slider-' + i + '" min="0.25" max="15" step="0.25" value="5">' +
        '<span class="runtime-val" id="valve-runtime-' + i + '"></span>';
      configEl.appendChild(row);

      $('valve-slider-' + i).addEventListener('input', (e) => {
        $('valve-runtime-' + i).textContent = fmtMinutes(parseFloat(e.target.value));
        updateValveTotal();
      });
    });

    fillValveFields(valves);
    configBuilt = true;
  }

  // Populate the existing valve rows from a status `valves` array. Split out of
  // buildValveUI so a season switch can refresh the values without rebuilding.
  function fillValveFields(valves) {
    valves.forEach((v, i) => {
      $('valve-name-' + i).value = v.name;
      $('valve-slider-' + i).value = secToMin(v.runtime);
      $('valve-runtime-' + i).textContent = fmtMinutes(secToMin(v.runtime));
    });
    updateValveTotal();
  }

  function buildDayCheckboxes() {
    const el = $('day-checkboxes');
    el.innerHTML = '';
    DAY_NAMES.forEach((name, i) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'day-btn';
      btn.id = 'day-btn-' + i;
      btn.textContent = name;
      btn.addEventListener('click', () => {
        activeDays ^= (1 << i);
        btn.classList.toggle('active', !!(activeDays & (1 << i)));
      });
      el.appendChild(btn);
    });
  }

  function applyActiveDays(mask) {
    activeDays = mask;
    DAY_NAMES.forEach((_, i) => {
      const btn = $('day-btn-' + i);
      if (btn) btn.classList.toggle('active', !!(mask & (1 << i)));
    });
  }

  function toggleValve(i, on) {
    fetch('/api/valve/' + i, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ on: on }),
    }).then(refresh);
  }

  function applyStatus(s) {
    $('conn').textContent = 'online';
    $('conn').className = 'pill online';

    if (s.hostname) {
      $('hostname').textContent = s.hostname;
      document.title = s.hostname;
    }
    $('time').textContent = s.time;
    $('date').textContent = s.date;
    $('tz').textContent = s.timezone;
    lastTempF = s.tempF;
    lastAvgTempF = s.avgTempF;
    renderTemp();
    $('rssi').textContent = s.rssi + ' dBm';
    $('ip').textContent = s.ip;
    $('runtime').textContent = s.lastRunMinutes + ' min';

    $('water-toggle').checked = !s.disabled;
    $('water-state').textContent = s.disabled ? 'Watering OFF' : 'Watering ON';

    tempScaling = s.tempScaling;
    $('temp-scaling').textContent = 'Temperature scaling: ' + (tempScaling ? 'ON' : 'OFF');
    $('temp-scaling').classList.toggle('primary', tempScaling);

    $('log').textContent = s.log;

    if (!configBuilt) {
      buildValveUI(s.valves);
      buildDayCheckboxes();
      $('run-hour').value = s.runHour;
      $('run-minute').value = s.runMinute;
      renderRunTime12h();
      applyActiveDays(s.activeDays);
      curSeason = s.season;
      $('season').value = String(s.season);
    } else if (s.season !== curSeason && document.activeElement !== $('season')) {
      // Season changed on the device (e.g. another client) - resync the whole card.
      curSeason = s.season;
      $('season').value = String(s.season);
      $('run-hour').value = s.runHour;
      $('run-minute').value = s.runMinute;
      renderRunTime12h();
      applyActiveDays(s.activeDays);
      fillValveFields(s.valves);
    }

    s.valves.forEach((v, i) => {
      const btn = $('valve-btn-' + i);
      if (btn) btn.classList.toggle('active', v.on);
    });

    $('wifi-ssid').placeholder = s.ssid || '';
    if (s.timezoneCode) $('timezone').value = s.timezoneCode;
  }

  function refresh() {
    fetch('/api/status')
      .then((r) => r.json())
      .then(applyStatus)
      .catch(() => {
        $('conn').textContent = 'offline';
        $('conn').className = 'pill offline';
      });
  }

  $('water-toggle').addEventListener('change', (e) => {
    fetch('/api/watering', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ disable: !e.target.checked }),
    }).then(refresh);
  });

  $('run-now').addEventListener('click', () => {
    fetch('/api/run', { method: 'POST' }).then(refresh);
  });

  $('temp-scaling').addEventListener('click', () => {
    fetch('/api/tempscaling', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled: !tempScaling }),
    }).then(refresh);
  });

  // Changing the season loads that profile's saved schedule and applies it.
  $('season').addEventListener('change', (e) => {
    const season = parseInt(e.target.value, 10);
    fetch('/api/season', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ season: season }),
    })
      .then(() => fetch('/api/status'))
      .then((r) => r.json())
      .then((s) => {
        curSeason = s.season;
        $('season').value = String(s.season);
        $('run-hour').value = s.runHour;
        $('run-minute').value = s.runMinute;
        renderRunTime12h();
        applyActiveDays(s.activeDays);
        fillValveFields(s.valves);
      });
  });

  $('save-schedule').addEventListener('click', () => {
    const valves = [];
    for (let i = 0; i < numValves; i++) {
      valves.push({
        name: $('valve-name-' + i).value,
        runtime: minToSec(parseFloat($('valve-slider-' + i).value)),
      });
    }
    const body = {
      season: curSeason,
      hour: parseInt($('run-hour').value, 10),
      minute: parseInt($('run-minute').value, 10),
      activeDays: activeDays,
      valves: valves,
    };
    fetch('/api/schedule', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    }).then(() => {
      const label = $('season').selectedOptions[0].text;
      $('save-schedule-status').textContent = 'Saved ' + label;
      setTimeout(() => { $('save-schedule-status').textContent = ''; }, 2000);
    });
  });

  $('save-wifi').addEventListener('click', () => {
    fetch('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ssid: $('wifi-ssid').value,
        pass: $('wifi-pass').value,
      }),
    }).then(() => {
      $('save-wifi-status').textContent = 'Saved';
      setTimeout(() => { $('save-wifi-status').textContent = ''; }, 2000);
    });
  });

  $('timezone').addEventListener('change', (e) => {
    fetch('/api/timezone', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ tz: e.target.value }),
    }).then(refresh);
  });

  $('reboot').addEventListener('click', () => {
    if (confirm('Reboot Sprinky now?')) {
      fetch('/api/reboot', { method: 'POST' });
    }
  });

  refresh();
  setInterval(refresh, 1000);
})();
)JS";
