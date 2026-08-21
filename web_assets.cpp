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
  <h1>🌱 Sprinky</h1>
  <span id="conn" class="pill offline">offline</span>
</header>

<nav class="tabs">
  <button class="tab-btn active" data-tab="status">Status</button>
  <button class="tab-btn" data-tab="valves">Valves</button>
  <button class="tab-btn" data-tab="setup">Setup</button>
</nav>

<main>
  <section id="tab-status" class="tab active">
    <div class="grid">
      <div class="card">
        <div class="card-label">Time</div>
        <div id="time" class="card-value">--:--:--</div>
        <div id="tz" class="card-sub"></div>
      </div>
      <div class="card">
        <div class="card-label">Outside Temp</div>
        <div id="temp" class="card-value">-- °F</div>
        <div id="avgtemp" class="card-sub"></div>
      </div>
      <div class="card">
        <div class="card-label">WiFi Signal</div>
        <div id="rssi" class="card-value">-- dBm</div>
      </div>
      <div class="card">
        <div class="card-label">Last Run</div>
        <div id="runtime" class="card-value">--</div>
      </div>
    </div>

    <div class="card wide">
      <div class="switch-row">
        <div>
          <div class="card-label">Watering</div>
          <div id="water-state" class="card-sub"></div>
        </div>
        <label class="switch">
          <input type="checkbox" id="water-toggle">
          <span class="slider"></span>
        </label>
      </div>
    </div>

    <div class="card wide">
      <div class="card-label">Log</div>
      <pre id="log"></pre>
    </div>
  </section>

  <section id="tab-valves" class="tab">
    <div class="card wide">
      <div class="card-label">Diagnostics — open a valve for two minutes</div>
      <div id="valve-buttons" class="valve-buttons"></div>
    </div>

    <div class="card wide">
      <div class="card-label">Schedule</div>
      <div class="form-row">
        <label for="run-hour">Run hour</label>
        <input type="number" id="run-hour" min="0" max="23">
        <label for="run-minute">Run minute</label>
        <input type="number" id="run-minute" min="0" max="59">
      </div>
      <div class="card-label">Days to Water</div>
      <div id="day-checkboxes" class="day-checkboxes"></div>
      <button id="run-now" class="btn">Run Watering Sequence Now</button>
    </div>

    <div class="card wide">
      <div class="card-label">Valve Names &amp; Run Times</div>
      <div id="valve-config"></div>
      <button id="save-schedule" class="btn primary">Save Schedule</button>
      <span id="save-schedule-status" class="save-status"></span>
    </div>
  </section>

  <section id="tab-setup" class="tab">
    <div class="card wide">
      <div class="card-label">WiFi Credentials</div>
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
      <div class="card-label">Time Zone</div>
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
        </select>
      </div>
    </div>

    <div class="card wide">
      <div class="card-label">Maintenance</div>
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
  --bg: #f2f4f3;
  --card-bg: #ffffff;
  --text: #1a1f1c;
  --text-sub: #6b7570;
  --accent: #2e7d5b;
  --accent-contrast: #ffffff;
  --border: #e1e5e2;
  --danger: #c0392b;
  --online: #2e7d5b;
  --offline: #b0392b;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #14171a;
    --card-bg: #1e2226;
    --text: #eceff1;
    --text-sub: #97a1a8;
    --accent: #45b587;
    --accent-contrast: #0b120d;
    --border: #2b3136;
    --danger: #e57373;
    --online: #45b587;
    --offline: #e57373;
  }
}
* { box-sizing: border-box; }
body {
  margin: 0;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  background: var(--bg);
  color: var(--text);
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 1rem 1.25rem;
}
header h1 { font-size: 1.25rem; margin: 0; }
.pill {
  font-size: 0.75rem;
  padding: 0.25rem 0.6rem;
  border-radius: 1rem;
  font-weight: 600;
}
.pill.online { background: var(--online); color: var(--accent-contrast); }
.pill.offline { background: var(--offline); color: var(--accent-contrast); }

.tabs {
  display: flex;
  gap: 0.5rem;
  padding: 0 1rem;
  border-bottom: 1px solid var(--border);
}
.tab-btn {
  background: none;
  border: none;
  color: var(--text-sub);
  font-size: 0.95rem;
  padding: 0.6rem 0.9rem;
  cursor: pointer;
  border-bottom: 2px solid transparent;
}
.tab-btn.active {
  color: var(--accent);
  border-bottom-color: var(--accent);
  font-weight: 600;
}
main { padding: 1rem; max-width: 720px; margin: 0 auto; }
.tab { display: none; }
.tab.active { display: block; }

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
  gap: 0.75rem;
  margin-bottom: 0.75rem;
}
.card {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: 0.75rem;
  padding: 0.9rem 1rem;
}
.card.wide { margin-bottom: 0.75rem; }
.card-label {
  font-size: 0.75rem;
  color: var(--text-sub);
  text-transform: uppercase;
  letter-spacing: 0.03em;
  margin-bottom: 0.35rem;
}
.card-value { font-size: 1.5rem; font-weight: 600; }
.card-sub { font-size: 0.8rem; color: var(--text-sub); margin-top: 0.2rem; }

.switch-row { display: flex; align-items: center; justify-content: space-between; }
.switch { position: relative; display: inline-block; width: 48px; height: 28px; }
.switch input { opacity: 0; width: 0; height: 0; }
.switch .slider {
  position: absolute; cursor: pointer; inset: 0;
  background: var(--border); border-radius: 28px; transition: 0.15s;
}
.switch .slider::before {
  content: ""; position: absolute; height: 22px; width: 22px; left: 3px; bottom: 3px;
  background: var(--card-bg); border-radius: 50%; transition: 0.15s;
}
.switch input:checked + .slider { background: var(--accent); }
.switch input:checked + .slider::before { transform: translateX(20px); }

pre#log {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 0.75rem;
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 200px;
  overflow-y: auto;
  margin: 0;
  color: var(--text-sub);
}

.valve-buttons {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
  gap: 0.6rem;
}
.valve-btn {
  border: 1px solid var(--border);
  background: var(--card-bg);
  color: var(--text);
  border-radius: 0.6rem;
  padding: 0.9rem 0.5rem;
  font-size: 0.9rem;
  cursor: pointer;
  transition: 0.15s;
}
.valve-btn.active {
  background: var(--accent);
  color: var(--accent-contrast);
  border-color: var(--accent);
}

.form-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  flex-wrap: wrap;
  margin-bottom: 0.6rem;
}
.form-row label { font-size: 0.85rem; color: var(--text-sub); min-width: 4.5rem; }
.form-row input[type="text"],
.form-row input[type="password"],
.form-row input[type="number"],
.form-row select {
  flex: 1;
  min-width: 6rem;
  padding: 0.5rem 0.6rem;
  border: 1px solid var(--border);
  border-radius: 0.5rem;
  background: var(--bg);
  color: var(--text);
  font-size: 0.9rem;
}
.form-row input[type="number"] { max-width: 5rem; flex: none; }
.form-row input[type="range"] { flex: 1; }

.day-checkboxes {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem;
  margin-bottom: 0.8rem;
}
.day-btn {
  border: 1px solid var(--border);
  background: var(--card-bg);
  color: var(--text);
  border-radius: 0.5rem;
  padding: 0.5rem 0.7rem;
  font-size: 0.85rem;
  cursor: pointer;
}
.day-btn.active {
  background: var(--accent);
  color: var(--accent-contrast);
  border-color: var(--accent);
}

.valve-row {
  display: flex;
  align-items: center;
  gap: 0.6rem;
  padding: 0.5rem 0;
  border-bottom: 1px solid var(--border);
}
.valve-row:last-child { border-bottom: none; }
.valve-row input[type="text"] {
  width: 8rem;
  padding: 0.4rem 0.5rem;
  border: 1px solid var(--border);
  border-radius: 0.5rem;
  background: var(--bg);
  color: var(--text);
  font-size: 0.85rem;
}
.valve-row .runtime-val { min-width: 3.5rem; text-align: right; font-variant-numeric: tabular-nums; font-size: 0.85rem; color: var(--text-sub); }

.btn {
  display: inline-block;
  background: var(--card-bg);
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 0.5rem;
  padding: 0.55rem 1rem;
  font-size: 0.9rem;
  cursor: pointer;
  text-decoration: none;
}
.btn.primary { background: var(--accent); color: var(--accent-contrast); border-color: var(--accent); }
.btn.danger { background: var(--danger); color: var(--accent-contrast); border-color: var(--danger); }
.save-status { font-size: 0.8rem; color: var(--text-sub); margin-left: 0.6rem; }
)CSS";

const char APP_JS[] PROGMEM = R"JS(
(() => {
  let numValves = 0;
  let configBuilt = false;
  let activeDays = 0x7F;
  const DAY_NAMES = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

  const $ = (id) => document.getElementById(id);

  document.querySelectorAll('.tab-btn').forEach((btn) => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.tab-btn').forEach((b) => b.classList.remove('active'));
      document.querySelectorAll('.tab').forEach((t) => t.classList.remove('active'));
      btn.classList.add('active');
      $('tab-' + btn.dataset.tab).classList.add('active');
    });
  });

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
        '<input type="text" id="valve-name-' + i + '" value="" maxlength="14">' +
        '<input type="range" id="valve-slider-' + i + '" min="1" max="600" value="300">' +
        '<span class="runtime-val" id="valve-runtime-' + i + '"></span>';
      configEl.appendChild(row);

      $('valve-name-' + i).value = v.name;
      $('valve-slider-' + i).value = v.runtime;
      $('valve-runtime-' + i).textContent = v.runtime + 's';

      $('valve-slider-' + i).addEventListener('input', (e) => {
        $('valve-runtime-' + i).textContent = e.target.value + 's';
      });
    });

    configBuilt = true;
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

    $('time').textContent = s.time;
    $('tz').textContent = s.timezone;
    $('temp').textContent = s.tempF + ' °F';
    $('avgtemp').textContent = '24h avg: ' + s.avgTempF.toFixed(1) + ' °F';
    $('rssi').textContent = s.rssi + ' dBm';
    $('runtime').textContent = s.lastRunMinutes + ' min';

    $('water-toggle').checked = !s.disabled;
    $('water-state').textContent = s.disabled ? 'Watering OFF' : 'Watering ON';

    $('log').textContent = s.log;

    if (!configBuilt) {
      buildValveUI(s.valves);
      buildDayCheckboxes();
      $('run-hour').value = s.runHour;
      $('run-minute').value = s.runMinute;
      applyActiveDays(s.activeDays);
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

  $('save-schedule').addEventListener('click', () => {
    const valves = [];
    for (let i = 0; i < numValves; i++) {
      valves.push({
        name: $('valve-name-' + i).value,
        runtime: parseInt($('valve-slider-' + i).value, 10),
      });
    }
    const body = {
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
      $('save-schedule-status').textContent = 'Saved';
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
