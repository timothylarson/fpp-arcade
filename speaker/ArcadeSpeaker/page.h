// The speaker's web page. It's served as is; its script fills it in from
// /status every two seconds and talks to /volume, /test, /sync, /models,
// /scan and /save.
#pragma once

const char PAGE[] = R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Arcade Speaker</title>
<link rel="icon" id="icon" href="data:,">
<style>
:root{--bg:#111317;--card:#1a1d23;--line:#2b3039;--field:#22262e;--text:#e9ebf0;--muted:#98a1b0;
--accent:#3d7ff2;--ok:#2fb35a;--warn:#e0a020;--bad:#e5484d;--setup:#4a8df8;--r:12px}
@media (prefers-color-scheme:light){:root{--bg:#f2f3f5;--card:#fff;--line:#dde0e5;--field:#f6f7f9;
--text:#1b1e24;--muted:#646c7a}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:620px;margin:0 auto;padding:16px 16px 40px;display:flex;flex-direction:column;gap:16px}
header{padding:4px 2px 0}
.eyebrow{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--muted)}
h1{font-size:26px;margin:2px 0 10px;font-weight:650;overflow-wrap:anywhere}
h2{font-size:15px;margin:0 0 12px;color:var(--muted);font-weight:600;letter-spacing:.02em}
.pill{display:inline-flex;align-items:center;gap:8px;padding:5px 12px 5px 10px;border-radius:999px;
font-size:14px;font-weight:600;background:var(--field);border:1px solid var(--line)}
.dot{width:10px;height:10px;border-radius:50%;background:var(--muted)}
.ready .dot{background:var(--ok)}.waiting .dot,.joining .dot{background:var(--warn)}
.nofpp .dot,.fault .dot,.offline .dot{background:var(--bad)}.setup .dot{background:var(--setup)}
.fault .dot,.setup .dot{animation:blink 1s steps(1) infinite}@keyframes blink{50%{opacity:.15}}
.banner{margin:12px 0 0;padding:10px 14px;border-radius:8px;border-left:4px solid var(--muted);
background:var(--card);color:var(--text)}
.ready+.banner{border-left-color:var(--ok)}.waiting+.banner,.joining+.banner{border-left-color:var(--warn)}
.nofpp+.banner,.fault+.banner,.offline+.banner{border-left-color:var(--bad)}.setup+.banner{border-left-color:var(--setup)}
section{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:16px}
.vol{display:flex;align-items:baseline;gap:6px;margin-bottom:6px}
.vol b{font-size:44px;line-height:1;font-weight:650;font-variant-numeric:tabular-nums}
.vol span{color:var(--muted);font-size:20px}
.volrow{display:flex;align-items:center;gap:10px}
input[type=range]{flex:1;height:44px;accent-color:var(--accent);margin:0}
button{font:inherit;font-weight:600;min-height:44px;padding:0 16px;border-radius:10px;cursor:pointer;
border:1px solid var(--line);background:var(--field);color:var(--text)}
button:hover{border-color:var(--muted)}button:disabled{opacity:.55;cursor:default}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.step{width:44px;padding:0;font-size:22px}
.actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:12px;align-items:center}
dl{margin:0;display:grid;grid-template-columns:auto 1fr;gap:10px 16px}
dt{color:var(--muted)}dd{margin:0;overflow-wrap:anywhere}
.sub{color:var(--muted);font-size:14px}
.bars{display:inline-flex;gap:2px;align-items:flex-end;height:14px;margin-right:6px;vertical-align:-1px}
.bars i{width:4px;background:var(--line);border-radius:1px}.bars i.on{background:var(--ok)}
.meter{height:6px;border-radius:3px;background:var(--line);margin-top:6px;overflow:hidden}
.meter i{display:block;height:100%;background:var(--accent)}
label{display:block;font-weight:600;margin:14px 0 6px}
label:first-of-type{margin-top:0}
.field{display:flex;gap:8px}
input[type=text],input[type=password]{flex:1;min-width:0;font:inherit;min-height:44px;padding:0 12px;
border-radius:10px;border:1px solid var(--line);background:var(--field);color:var(--text)}
input:focus,button:focus-visible{outline:2px solid var(--accent);outline-offset:1px}
.hint{color:var(--muted);font-size:14px;margin-top:6px}
.dirty{color:var(--warn);font-size:14px;display:none}
.msg{font-size:14px;color:var(--muted)}
footer{text-align:center;color:var(--muted);font-size:13px}
a{color:var(--accent)}
body.setupmode #settings{order:-1}
@media (max-width:340px){dl{grid-template-columns:1fr;gap:2px}dd{margin-bottom:8px}}
</style></head><body>
<main>
<header>
<div class="eyebrow">Arcade Speaker</div>
<h1 id="name">&nbsp;</h1>
<div id="pill" class="pill offline"><span class="dot"></span><span id="state">Connecting...</span></div>
<div id="banner" class="banner">Asking the speaker how it is.</div>
</header>

<section id="volume">
<h2>Volume</h2>
<div class="vol"><b id="volNum">-</b><span>%</span></div>
<div class="volrow">
<button class="step" id="volDown" aria-label="Quieter">&minus;</button>
<input type="range" id="vol" min="0" max="100" step="1" aria-label="Volume">
<button class="step" id="volUp" aria-label="Louder">+</button>
</div>
<div class="actions"><button id="test">Test sound</button></div>
</section>

<section id="status">
<h2>Status</h2>
<dl>
<dt>Plays for</dt><dd id="model">-</dd>
<dt>Address</dt><dd id="addr">-</dd>
<dt>Wi-Fi</dt><dd id="wifi">-</dd>
<dt>FPP</dt><dd id="fpp">-</dd>
<dt>Sounds</dt><dd id="sounds">-</dd>
<dt>Last sound</dt><dd id="last">-</dd>
<dt>Timing</dt><dd id="timing">-</dd>
<dt>Storage</dt><dd id="storage">-</dd>
</dl>
<div class="actions"><button id="sync">Fetch sounds now</button><span class="msg" id="syncMsg"></span></div>
</section>

<section id="settings">
<h2>Settings</h2>
<form id="form" autocomplete="off">
<label for="fName">Name</label>
<div class="field"><input type="text" id="fName" name="name" maxlength="40" required></div>
<div class="hint" id="nameHint">Shown on FPP's Arcade Games page, and gives its address.</div>

<label for="fModel">Plays for</label>
<div class="field"><input type="text" id="fModel" name="model" list="models" maxlength="60"></div>
<datalist id="models"></datalist>
<div class="hint">The overlay model this controller plays, as on the Arcade Games page. Only that model's sounds play here.</div>

<label for="fFpp">FPP player</label>
<div class="field"><input type="text" id="fFpp" name="fpp" inputmode="url" maxlength="60" placeholder="10.0.0.20"></div>
<div class="hint">Its IP address or name.</div>

<label for="fSsid">Wi-Fi network</label>
<div class="field"><input type="text" id="fSsid" name="ssid" list="ssids" maxlength="32">
<button type="button" id="scan">Scan</button></div>
<datalist id="ssids"></datalist>

<label for="fPass">Wi-Fi password</label>
<div class="field"><input type="password" id="fPass" name="pass" maxlength="64" placeholder="Unchanged">
<button type="button" id="show">Show</button></div>
<div class="hint">Leave blank to keep the current one.</div>

<div class="actions"><button class="primary" id="save">Save and restart</button>
<span class="dirty" id="dirty">Unsaved changes</span><span class="msg" id="saveMsg"></span></div>
</form>
</section>

<footer id="foot"></footer>
</main>
<script>
const $ = id => document.getElementById(id);
const STATES = {
  ready: ['Ready', s => 'Plays the sounds for ' + s.model + '.'],
  waiting: ['Waiting for FPP', s => 'On the Wi-Fi; asking FPP at ' + s.fpp + ' for its sounds.'],
  joining: ['Joining Wi-Fi', s => 'Joining ' + s.ssid + '...'],
  nofpp: ['Can\'t reach FPP', s => s.fpp ? 'FPP at ' + s.fpp + ' doesn\'t answer. Check the address below, and that the player is up.'
                                         : 'Enter the FPP player\'s address below.'],
  setup: ['Set-up mode', s => s.ssid ? 'Can\'t reach ' + s.ssid + '. Check the Wi-Fi settings below; it keeps trying meanwhile.'
                                     : 'Enter the Wi-Fi network and FPP player below, then Save.'],
  fault: ['Fault', s => s.fault],
  offline: ['Not answering', () => 'The speaker isn\'t answering. It may be restarting, or out of range.']
};
let dirty = false, dragging = false, filled = false, volTimer = 0, iconState = '', host = '';

// A speaker in the tab, in the status light's colour.
const ICON_COLOURS = {ready: '#2fb35a', waiting: '#e0a020', joining: '#e0a020', nofpp: '#e5484d',
                      fault: '#e5484d', offline: '#e5484d', setup: '#4a8df8'};
function setIcon(key) {
  if (key === iconState) return;
  iconState = key;
  const c = ICON_COLOURS[key];
  const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">' +
    '<rect width="32" height="32" rx="7" fill="#1a1d23"/>' +
    '<path d="M6 12.5h4.5L17 7v18l-6.5-5.5H6z" fill="' + c + '"/>' +
    '<path d="M21 11.5a6 6 0 0 1 0 9M24 8.5a10 10 0 0 1 0 15" fill="none" stroke="' + c +
    '" stroke-width="2.4" stroke-linecap="round"/></svg>';
  $('icon').href = 'data:image/svg+xml,' + encodeURIComponent(svg);
}

function ago(s) {
  if (s < 60) return s + ' s ago';
  if (s < 3600) return Math.round(s / 60) + ' min ago';
  return Math.round(s / 3600) + ' h ago';
}
function mb(b) { return (b / 1048576).toFixed(1); }
function bars(rssi) {
  const n = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  let h = '<span class="bars" aria-hidden="true">';
  for (let i = 1; i <= 4; i++) h += '<i class="' + (i <= n ? 'on' : '') + '" style="height:' + (3 + i * 3) + 'px"></i>';
  return h + '</span>';
}
function setState(key, s) {
  $('pill').className = 'pill ' + key;
  setIcon(key);
  $('state').textContent = STATES[key][0];
  $('banner').textContent = STATES[key][1](s);
}
function text(id, t, sub) {
  const el = $(id);
  el.textContent = t;
  if (sub) { const d = document.createElement('div'); d.className = 'sub'; d.textContent = sub; el.appendChild(d); }
}

function render(s) {
  document.body.classList.toggle('setupmode', s.state === 'setup');
  host = s.host;
  $('name').textContent = s.name || 'Arcade Speaker';
  document.title = (s.name || 'Arcade Speaker') + ' - Arcade Speaker';
  setState(s.state, s);
  if (!dragging) { $('vol').value = s.volume; $('volNum').textContent = s.volume; }
  text('model', s.model || 'Not set');
  if (s.connected) {
    $('wifi').innerHTML = bars(s.rssi);
    $('wifi').append(s.ssid + ' (' + s.rssi + ' dBm)');
    if (s.powerSave) {
      const w = document.createElement('div'); w.className = 'sub'; w.textContent = 'Power saving is on: sounds may lag';
      $('wifi').appendChild(w);
    }
    const a = document.createElement('a'); a.href = 'http://' + s.host + '.local/'; a.textContent = s.host + '.local';
    $('addr').replaceChildren(a);
    const d = document.createElement('div'); d.className = 'sub'; d.textContent = s.ip; $('addr').appendChild(d);
  } else {
    text('addr', s.ap ? '192.168.4.1' : '-', s.ap ? 'on its set-up network' : '');
    text('wifi', s.ssid ? 'Not connected to ' + s.ssid : 'Not set up', s.ap ? 'Set-up network: ' + s.ap : '');
  }
  text('fpp', s.fpp || 'Not set', s.fpp ? (s.fppOk ? 'Answering' : s.fppAsked ? 'Not answering' : 'Not asked yet') : '');
  text('sounds', s.sounds + ' loaded', s.sync === s.sounds + ' sounds from FPP' ? '' : s.sync);
  if (s.events) {
    const m = s.last.match(/^PLAY \S+ (\S+) (\S+)/);
    text('last', m ? m[1] : s.last, (m ? 'at ' + Math.round(m[2] * 100) + '% from FPP, ' : '') + ago(s.lastAgo));
  } else {
    text('last', 'None yet');
  }
  // How the Wi-Fi is treating the sounds: FPP stamps each one with its time.
  if (s.timed) {
    const pct = n => (n * 100 / s.timed).toFixed(n * 100 / s.timed < 10 ? 1 : 0) + '%';
    text('timing', s.lateSkipped ? s.lateSkipped + ' late sounds skipped' : 'No late sounds',
         pct(s.late50) + ' of ' + s.timed + ' arrived over 50 ms late; the latest ' + s.lateWorst + ' ms');
  } else {
    text('timing', s.events ? 'FPP doesn\'t time its sounds' : 'No sounds yet',
         s.events ? 'Its arcade plugin is older than this speaker' : '');
  }
  $('storage').textContent = mb(s.used) + ' of ' + mb(s.total) + ' MB';
  const meter = document.createElement('div'); meter.className = 'meter';
  meter.innerHTML = '<i style="width:' + Math.round(100 * s.used / Math.max(1, s.total)) + '%"></i>';
  $('storage').appendChild(meter);
  $('foot').textContent = 'Firmware ' + s.firmware + ' · up ' + ago(s.uptime).replace(' ago', '');
  if (!filled || !dirty) {
    $('fName').value = s.name; $('fModel').value = s.model; $('fFpp').value = s.fpp; $('fSsid').value = s.ssid;
    nameHint();
    if (!filled && s.connected) loadModels();
    filled = true;
  }
}

async function refresh() {
  try {
    const r = await fetch('/status', {cache: 'no-store'});
    render(await r.json());
  } catch (e) {
    setState('offline', {});
  }
}

async function loadModels() {
  try {
    const list = await (await fetch('/models')).json();
    $('models').replaceChildren(...list.map(n => Object.assign(document.createElement('option'), {value: n})));
  } catch (e) {}
}

function sendVolume(save) {
  clearTimeout(volTimer);
  volTimer = setTimeout(() => fetch('/volume?v=' + $('vol').value + (save ? '&save=1' : '')), save ? 0 : 80);
}
$('vol').addEventListener('input', () => { dragging = true; $('volNum').textContent = $('vol').value; sendVolume(false); });
$('vol').addEventListener('change', () => { dragging = false; sendVolume(true); });
function step(d) {
  $('vol').value = Math.max(0, Math.min(100, +$('vol').value + d));
  $('volNum').textContent = $('vol').value;
  sendVolume(true);
}
$('volDown').onclick = () => step(-5);
$('volUp').onclick = () => step(5);
$('test').onclick = () => fetch('/test');

$('sync').onclick = async () => {
  const b = $('sync');
  b.disabled = true; b.textContent = 'Fetching...'; $('syncMsg').textContent = 'The first time takes about a minute.';
  try { $('syncMsg').textContent = await (await fetch('/sync')).text(); }
  catch (e) { $('syncMsg').textContent = 'Didn\'t finish; try again.'; }
  b.disabled = false; b.textContent = 'Fetch sounds now';
  refresh();
};

$('scan').onclick = async () => {
  const b = $('scan');
  b.disabled = true; b.textContent = 'Scanning...';
  try {
    const list = await (await fetch('/scan')).json();
    $('ssids').replaceChildren(...list.map(n => Object.assign(document.createElement('option'), {value: n.ssid, label: n.rssi + ' dBm'})));
    b.textContent = list.length + ' found';
    $('fSsid').focus();
  } catch (e) { b.textContent = 'Scan'; }
  b.disabled = false;
  setTimeout(() => { b.textContent = 'Scan'; }, 3000);
};
$('show').onclick = () => {
  const p = $('fPass'), hide = p.type === 'text';
  p.type = hide ? 'password' : 'text';
  $('show').textContent = hide ? 'Show' : 'Hide';
};

// Its address from a name, as the speaker makes it (hostName() there).
function hostFor(name) {
  return name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+/, '').slice(0, 32).replace(/-+$/, '');
}
// Says where the page will be, as the name is typed.
function nameHint() {
  const h = hostFor($('fName').value);
  $('nameHint').textContent = h ? 'Shown on FPP\'s Arcade Games page. Its address will be http://' + h + '.local'
                                : 'Shown on FPP\'s Arcade Games page, and gives its address.';
}
$('fName').addEventListener('input', nameHint);
$('form').addEventListener('input', () => { dirty = true; $('dirty').style.display = 'inline'; });
$('form').onsubmit = async e => {
  e.preventDefault();
  $('save').disabled = true;
  const h = hostFor($('fName').value) || host;
  // A new name is a new address; otherwise come back to this one.
  const target = h !== host || location.hostname.endsWith('.local') ? 'http://' + h + '.local/' : location.origin + '/';
  try {
    await fetch('/save', {method: 'POST', body: new URLSearchParams(new FormData($('form')))});
  } catch (e) {
    $('saveMsg').textContent = 'Couldn\'t save; try again.';
    $('save').disabled = false;
    return;
  }
  dirty = false; $('dirty').style.display = 'none';
  $('saveMsg').textContent = 'Saved. Restarting; this page follows it to ' + target + ' when it\'s back.';
  // Ask here until it's restarted under its new name (this address keeps
  // working for a while: the IP is the same and the old name is remembered),
  // then go there. If it can't be told, go anyway.
  const wait = ms => new Promise(r => setTimeout(r, ms));
  // It restarts half a second after saving, so anything heard after 3 s is
  // the new start.
  let goTo = target;
  await wait(3000);
  for (const until = Date.now() + 25000; Date.now() < until; await wait(1000)) {
    try {
      const s = await (await fetch('/status', {cache: 'no-store'})).json();
      if (h !== host) goTo = 'http://' + s.host + '.local/';   // "-2" on the end if the name was taken
      break;
    } catch (e) {}
  }
  location.href = goTo;
};

refresh();
setInterval(refresh, 2000);
</script>
</body></html>
)PAGE";
