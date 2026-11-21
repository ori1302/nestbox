#include "door/page.h"

namespace nestbox {

namespace {

// The docs' bench colours (the product's own), the goose's beak for what is on or chosen, the system's type, and
// the pet's name in the screen's own font (an SVG the server draws).
const char* const STYLE = R"CSS(
:root{--bg:#e3e4e0;--raised:#eeefeb;--line:#c9ccc7;--ink:#1d2433;--soft:#596071;--beak:#f39c34;--bezel:#1d2433;
--warn:#a3341f;color-scheme:light}
@media(prefers-color-scheme:dark){:root{--bg:#16181a;--raised:#1e2124;--line:#33383d;--ink:#e8eaeb;--soft:#a2a9ae;
--bezel:#0d0f11;--warn:#ff8f73;color-scheme:dark}}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;
-webkit-text-size-adjust:100%}
main{max-width:31rem;margin:0 auto;padding:28px max(16px,env(safe-area-inset-right)) 40px max(16px,env(safe-area-inset-left))}
.hero{display:flex;align-items:center;gap:18px;margin:0 0 22px}
.glass{flex:none;width:92px;height:92px;border-radius:20px;background:var(--bezel);display:grid;place-items:center;
transition:filter .6s}
.glass svg{width:60px;height:60px}
.glass[data-s=dim]{filter:brightness(.62)}.glass[data-s=off] svg{opacity:0}
h1{margin:0;font-size:1.9rem;line-height:1.1;overflow-wrap:anywhere}
h1 svg{display:block;height:2.1rem;width:auto;max-width:100%}
.state{margin:6px 0 0;color:var(--soft)}
h2{font-size:1.05rem;margin:28px 2px 8px;display:flex;justify-content:space-between;align-items:baseline;gap:8px}
h2 small{font-weight:400;color:var(--soft);font-size:.9rem}
h2 button{min-height:32px;font-size:.9rem}
.panel{background:var(--raised);border-radius:16px;box-shadow:inset 0 0 0 1px var(--line);padding:4px 16px}
.row{display:flex;align-items:center;gap:10px;min-height:52px;padding:8px 0}
.row+.row,.row+.more,.more+.row{border-top:1px solid var(--line)}
.grow{flex:1;min-width:0;overflow-wrap:anywhere}
.soft{color:var(--soft)}
.mark{flex:none;width:10px;height:10px;border-radius:2px;background:var(--beak)}
.mark.off{background:transparent;box-shadow:inset 0 0 0 1.5px var(--line)}
button,.btn{font:inherit;font-size:.95rem;font-weight:600;min-height:40px;padding:0 16px;border-radius:999px;
border:1.5px solid var(--ink);background:none;color:var(--ink);cursor:pointer;text-decoration:none;display:inline-grid;
place-items:center;white-space:nowrap}
button.main{background:var(--ink);color:var(--bg)}
button.quiet{border-color:transparent;color:var(--soft);font-weight:500;padding:0 8px}
button.warn{border-color:var(--warn);color:var(--warn)}
button:disabled,input:disabled{opacity:.45;cursor:default}
:focus-visible{outline:3px solid var(--beak);outline-offset:2px}
.wide{width:100%;margin:12px 0}
.ctl{padding:12px 0}.ctl+.ctl{border-top:1px solid var(--line)}
.ctl .top{display:flex;justify-content:space-between;align-items:baseline;gap:12px;margin:0 0 8px}
.ctl label{font-weight:600}.ctl output{color:var(--soft);font-variant-numeric:tabular-nums}
.hint{margin:6px 0 0;color:var(--soft);font-size:.9rem}
input[type=range]{width:100%;accent-color:var(--beak);margin:4px 0;height:28px}
.seg{display:grid;grid-template-columns:repeat(auto-fit,minmax(58px,1fr));background:var(--bg);border-radius:12px;
padding:3px;gap:2px}
.seg button{min-width:0;min-height:38px;padding:0 4px;border:0;border-radius:9px;font-weight:500;color:var(--soft)}
.seg button[aria-pressed=true]{background:var(--raised);color:var(--ink);font-weight:650;
box-shadow:0 0 0 1px var(--line),inset 0 -2px 0 var(--beak)}
.switch{appearance:none;flex:none;width:50px;height:30px;border-radius:99px;background:var(--line);position:relative;
margin:0;cursor:pointer;transition:background .2s}
.switch::after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:#fff;
transition:transform .2s}
.switch:checked{background:var(--beak)}.switch:checked::after{transform:translateX(20px)}
.times{display:flex;align-items:center;gap:8px;margin-top:10px}
input[type=time]{font:inherit;color:var(--ink);background:var(--bg);border:1px solid var(--line);border-radius:10px;
padding:6px 8px;min-height:40px}
.lock{display:flex;align-items:center;gap:14px;background:var(--ink);color:var(--bg);border-radius:16px;padding:14px 16px;
margin:0 0 6px}
.lock p{margin:0;flex:1}.lock button{border-color:var(--bg);color:var(--bg)}
.lock b{font-variant-numeric:tabular-nums}
dl{margin:0}dl .row{justify-content:space-between}dt{color:var(--soft)}dd{margin:0;text-align:right;overflow-wrap:anywhere}
code{font:600 .9rem ui-monospace,Menlo,monospace;overflow-wrap:anywhere}
.ask{background:var(--bg);border-radius:12px;padding:12px;margin:0 0 10px}
.ask p{margin:0 0 10px}.ask .acts{display:flex;gap:8px;flex-wrap:wrap}
.say{margin:8px 2px 0;color:var(--soft);font-size:.92rem}.say:empty{display:none}
a{color:inherit}.link{display:block;text-decoration:none}.link b{text-decoration:underline;text-underline-offset:3px}
.link span,.sub{display:block;color:var(--soft);font-size:.92rem}
footer{margin:30px 2px 0;color:var(--soft);font-size:.85rem}
footer button{min-height:0;padding:0;border:0;font-weight:400;font-size:inherit;color:inherit;text-decoration:underline}
@media(prefers-reduced-motion:reduce){*{transition:none!important}}
)CSS";

const char* const BODY = R"HTML(
<div class=lock id=lock hidden><p id=lock-text>You can look around. To change anything, allow this browser once.</p>
<button id=allow type=button>Allow changes</button></div>
<h2>Pets <button class=quiet id=edit type=button aria-pressed=false>Edit</button></h2>
<div class=panel><div id=pets></div>
<button class=wide id=add type=button>Add a pet&hellip;</button><input id=egg type=file accept=".egg" hidden></div>
<p class=say id=pets-say role=status></p>
<h2>Screen <small id=now-state></small></h2>
<div class=panel id=screen>
<div class=ctl><div class=top><label for=bright>Brightness</label><output id=bright-v></output></div>
<input type=range id=bright min=5 max=100 step=5></div>
<div class=ctl><div class=top><label>Dim after</label></div>
<div class=seg id=dim_after data-v="5 15 30 60 120 0" data-l="5 min|15 min|30 min|1 hr|2 hr|Never"></div>
<p class=hint>Counting from the last thing that happened: a message, your typing, a press.</p></div>
<div class=ctl><div class=top><label for=dim>Dimmed brightness</label><output id=dim-v></output></div>
<input type=range id=dim min=5 max=100 step=5></div>
<div class=ctl><div class=top><label>Turn off after</label></div>
<div class=seg id=off_after data-v="15 30 60 180 360 0" data-l="15 min|30 min|1 hr|3 hr|6 hr|Never"></div>
<p class=hint>Anything that happens wakes it, and a press on a dark screen only wakes it.</p></div>
<div class=ctl><div class=top><label for=night>Night hours</label><input class=switch type=checkbox id=night role=switch></div>
<div class=times id=times hidden><label for=from class=soft>From</label><input type=time id=from>
<label for=to class=soft>to</label><input type=time id=to></div>
<p class=hint>At night the screen dims after a minute and turns off after ten.</p></div>
<div class=ctl><div class=top><label for=care>Burn-in care</label><input class=switch type=checkbox id=care role=switch></div>
<p class=hint id=care-hint></p></div>
</div>
<p class=say id=screen-say role=status></p>
<h2>This NestBox</h2>
<div class=panel><dl id=facts></dl><div id=dev-ask></div>
<div class=row><button id=restart type=button>Restart</button><span class=grow></span>
<button class=warn id=forget type=button>Forget Wi-Fi</button></div></div>
<p class=say id=dev-say role=status></p>
<h2>Connect</h2>
<div class=panel>
<div class=row><a class="link grow" href="https://nestbox.pet"><b>NestBox for Mac</b><span>Lets the pet follow your typing
and your Mac's sleep.</span></a></div>
<div class=row><div class=grow><b>Coding agents</b><span class=sub>Give your agent this address and it sets
itself up:</span><code id=agents></code></div><button class=quiet id=copy type=button>Copy</button></div>
<div class=row><a class="link grow" href="/api"><b>HTTP API</b><span>Every route, with examples.</span></a></div>
</div>
<footer id=foot></footer>
)HTML";

const char* const SCRIPT = R"JS(
const $ = id => document.getElementById(id), KEY = 'nestbox-key';
let key = '', dev = null, busy = false, pets = null, editing = false;
try { key = localStorage.getItem(KEY) || ''; } catch (e) {}
const keep = k => { key = k; try { k ? localStorage.setItem(KEY, k) : localStorage.removeItem(KEY); } catch (e) {} };
const say = (id, t) => { $(id).textContent = t || ''; };
async function api(method, path, body) {
  const h = key ? {Authorization: 'Bearer ' + key} : {};
  if (body && !(body instanceof Blob)) body = JSON.stringify(body);
  const r = await fetch(path, {method, body, headers: h, cache: 'no-store'});
  let j = {};
  try { j = await r.json(); } catch (e) {}
  if (r.status === 401) { if (key) keep(''); render(); throw new Error('Allow changes from this browser first.'); }
  if (!r.ok) throw new Error(j.error || 'NestBox answered ' + r.status);
  return j;
}
const mins = m => m === 0 ? 'never' : m < 60 ? m + ' min' : m / 60 + (m === 60 ? ' hour' : ' hours');
const STATE = {awake: 'Awake', dim: 'Dimmed', off: 'Off'};
function render() {
  const can = !!key;
  $('lock').hidden = can;
  document.querySelectorAll('#screen input, #screen .seg button, #add, #edit, #restart, #forget, #pets button')
    .forEach(b => { b.disabled = !can; });
  $('foot').replaceChildren();
  if (can) {
    const f = document.createElement('button'); f.type = 'button'; f.textContent = 'Stop allowing changes here';
    f.onclick = () => { keep(''); render(); }; $('foot').append(f);
  }
}
function seg(id, value, send) {
  const el = $(id), vals = el.dataset.v.split(' ').map(Number), labels = el.dataset.l.split('|');
  if (!el.children.length) vals.forEach((v, i) => {
    const b = document.createElement('button'); b.type = 'button'; b.textContent = labels[i]; b.dataset.v = v;
    b.onclick = () => { seg(id, v); send(v); }; el.append(b);
  });
  [...el.children].forEach(b => b.setAttribute('aria-pressed', String(+b.dataset.v === value)));
}
const hhmm = t => t === 'off' ? null : t.split('-');
function show(j) {
  dev = j.device;
  const s = dev.screen, pet = pets && pets.pets.find(p => p.name === j.now.pet);
  const name = pet ? pet.title || pet.name : j.now.pet;
  $('glass').dataset.s = s.state;
  $('state').textContent = name + ' is out. The screen is ' + (s.state === 'awake' ? 'awake' : s.state === 'dim' ?
    'dimmed' : 'off') + (s.state === 'off' ? '.' : ', at ' + s.backlight + ' %.');
  $('now-state').textContent = STATE[s.state];
  if (document.activeElement !== $('bright')) { $('bright').value = s.bright; $('bright-v').value = s.bright + ' %'; }
  if (document.activeElement !== $('dim')) { $('dim').value = s.dim; $('dim-v').value = s.dim + ' %'; }
  seg('dim_after', s.dim_after, v => set({dim_after: v}));
  seg('off_after', s.off_after, v => set({off_after: v}));
  $('care').checked = s.care;
  $('care-hint').textContent = s.oled ?
    'Moves the picture a pixel every few minutes, and lets the status bar and gauges go while they stay the same, ' +
    'so nothing burns into this OLED screen.' :
    'Lets the status bar and gauges go while they stay the same. Made for OLED screens; this one is LCD.';
  const n = hhmm(s.night);
  if (!$('times').contains(document.activeElement)) {
    $('night').checked = !!n; $('times').hidden = !n;
    if (n) { $('from').value = n[0]; $('to').value = n[1]; }
  }
  const host = location.host, facts = [['Address', host], ['Name', dev.name + '.local']];
  if (dev.temperature_c !== undefined) facts.push(['Temperature', dev.temperature_c + ' °C']);
  if (dev.wifi) facts.push(['Wi-Fi signal', dev.wifi[0].toUpperCase() + dev.wifi.slice(1)]);
  if (j.uptime_s !== undefined) facts.push(['Running for', since(j.uptime_s)]);
  facts.push(['Firmware', dev.firmware]);
  $('facts').replaceChildren(...facts.map(([k, v]) => {
    const r = document.createElement('div'), dt = document.createElement('dt'), dd = document.createElement('dd');
    r.className = 'row'; dt.textContent = k; dd.textContent = v; r.append(dt, dd); return r;
  }));
  $('agents').textContent = 'http://' + host + '/agents';
  render();
}
function since(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
  return d ? d + (d === 1 ? ' day' : ' days') + (h ? ', ' + h + ' h' : '') : h ? h + ' h ' + m + ' min' : m + ' min';
}
async function refresh() {
  try { show(await api('GET', '/v1')); } catch (e) { $('state').textContent = e.message; }
}
async function set(screen) {
  try {
    const j = await api('POST', '/v1', {screen});
    say('screen-say', (j.warnings && j.warnings[0]) || 'Saved.');
    await refresh();
  } catch (e) { say('screen-say', e.message); }
}
function petRow(p, out) {
  const r = document.createElement('div'), mark = document.createElement('span'), name = document.createElement('span');
  r.className = 'row'; mark.className = 'mark' + (p.name === out ? '' : ' off'); name.className = 'grow';
  name.textContent = p.title || p.name; r.append(mark, name);
  if (editing && pets.pets.length > 1) {
    const off = document.createElement('button'); off.type = 'button'; off.className = 'warn'; off.textContent = 'Take off';
    off.setAttribute('aria-label', 'Take ' + (p.title || p.name) + ' off');
    off.onclick = () => confirmRow(r, 'Take ' + (p.title || p.name) + ' off this NestBox? Its file is deleted from it.',
      'Take off', () => act(async () => { await api('DELETE', '/v1/pets/' + p.name); return watch(); }), true);
    r.append(off);
  } else if (p.name === out) {
    const t = document.createElement('span'); t.className = 'soft'; t.textContent = 'Out now'; r.append(t);
  } else if (!editing) {
    const b = document.createElement('button'); b.type = 'button'; b.textContent = 'Bring out';
    b.onclick = () => act(async () => { await api('POST', '/v1', {pet: p.name}); return (p.title || p.name) + ' is coming out.'; });
    r.append(b);
  }
  return r;
}
function confirmRow(after, text, yes, run, warn) {
  const a = document.createElement('div'), p = document.createElement('p'), acts = document.createElement('div');
  const y = document.createElement('button'), n = document.createElement('button');
  a.className = 'ask more'; acts.className = 'acts'; p.textContent = text;
  y.type = n.type = 'button'; y.textContent = yes; y.className = warn ? 'warn' : 'main'; n.textContent = 'Keep';
  n.className = 'quiet'; y.onclick = () => { a.remove(); run(); }; n.onclick = () => a.remove();
  acts.append(y, n); a.append(p, acts);
  document.querySelectorAll('.ask').forEach(x => x.remove());
  after.after(a); y.focus();
}
async function loadPets() {
  try {
    pets = await api('GET', '/v1/pets');
    $('pets').replaceChildren(...pets.pets.map(p => petRow(p, pets.out)));
    render();
    return pets;
  } catch (e) { say('pets-say', e.message); return null; }
}
async function act(run) {
  if (busy) return;
  busy = true;
  try { say('pets-say', await run()); } catch (e) { say('pets-say', e.message); }
  busy = false;
  await loadPets(); refresh();
}
async function watch() {
  for (;;) {
    const j = await loadPets();
    if (!j || !j.change) {
      const last = (j && j.last_change) || '';
      return last.startsWith('added ') ? 'Added.' : last.startsWith('took off ') ? 'Taken off.' :
        last.replace(/^[a-z0-9-]+: /, 'Not done: ');
    }
    say('pets-say', 'Saving' + String.fromCharCode(8230));
    await new Promise(r => setTimeout(r, 1000));
  }
}
$('edit').onclick = () => {
  editing = !editing;
  $('edit').textContent = editing ? 'Done' : 'Edit'; $('edit').setAttribute('aria-pressed', String(editing));
  $('pets').replaceChildren(...pets.pets.map(p => petRow(p, pets.out))); render();
};
$('add').onclick = () => $('egg').click();
$('egg').onchange = () => {
  const f = $('egg').files[0]; $('egg').value = '';
  if (!f) return;
  const id = f.name.replace(/\.egg$/i, '').toLowerCase().replace(/[^a-z0-9-]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 32);
  if (!/\.egg$/i.test(f.name) || !id) return say('pets-say', 'Choose a pet’s .egg file.');
  say('pets-say', 'Sending' + String.fromCharCode(8230));
  act(async () => { await api('PUT', '/v1/pets/' + id, f); return watch(); });
};
$('bright').oninput = () => { $('bright-v').value = $('bright').value + ' %'; };
$('dim').oninput = () => { $('dim-v').value = $('dim').value + ' %'; };
$('bright').onchange = () => set({bright: +$('bright').value});
$('dim').onchange = () => set({dim: +$('dim').value});
const night = () => set({night: $('night').checked ? ($('from').value || '23:00') + '-' + ($('to').value || '07:00') : 'off'});
$('night').onchange = () => { $('times').hidden = !$('night').checked; night(); };
$('from').onchange = $('to').onchange = night;
$('care').onchange = () => set({care: $('care').checked});
$('allow').onclick = async () => {
  $('allow').disabled = true;
  try {
    for (;;) {
      const j = await api('POST', '/v1/pair', {name: 'this browser'});
      if (j.key) { keep(j.key); break; }
      $('lock-text').innerHTML = 'Press the button on top of your NestBox. <b>' + j.ends_in + ' s</b>';
      await new Promise(r => setTimeout(r, 1000));
    }
    $('lock-text').textContent = '';
    render(); loadPets();
  } catch (e) { $('lock-text').textContent = e.message + ' Try again.'; }
  $('allow').disabled = false;
};
$('restart').onclick = () => confirmRow($('restart').parentNode, 'Restart NestBox? It is back in about 15 seconds.',
  'Restart', async () => {
    try { await api('POST', '/v1', {restart: true}); say('dev-say', 'Restarting' + String.fromCharCode(8230)); }
    catch (e) { return say('dev-say', e.message); }
    setTimeout(function back() { fetch('/v1', {cache: 'no-store'}).then(() => location.reload(), () => setTimeout(back, 2000)); }, 6000);
  });
$('forget').onclick = () => confirmRow($('forget').parentNode,
  'Forget this Wi-Fi? NestBox leaves your network and opens its own, so you can set it up again from its screen.',
  'Forget Wi-Fi', async () => {
    try { await api('POST', '/v1', {wifi: 'forget'}); say('dev-say', 'Forgetting this Wi-Fi. Set it up again from its screen.'); }
    catch (e) { say('dev-say', e.message); }
  }, true);
$('copy').onclick = () => {
  const t = $('agents').textContent;
  const done = () => { $('copy').textContent = 'Copied'; setTimeout(() => { $('copy').textContent = 'Copy'; }, 1500); };
  if (navigator.clipboard) navigator.clipboard.writeText(t).then(done, () => {});
};
loadPets().then(refresh);
setInterval(() => { if (!document.hidden) refresh(); }, 5000);
)JS";

}  // namespace

std::string html_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '&': o += "&amp;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

std::string device_page(const std::string& raw_name, const std::string& picture, const std::string& name_svg) {
    const std::string name = html_escape(raw_name);
    std::string html = "<!doctype html><html lang=en><meta charset=utf-8>"
                       "<meta name=viewport content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
                       "<meta name=color-scheme content=\"light dark\"><title>" + name + "</title><style>" + STYLE +
                       "</style><main><header class=hero><div class=glass id=glass>" + picture + "</div><div>";
    if (name_svg.empty()) html += "<h1 dir=auto>" + name + "</h1>";
    else html += "<h1 aria-label=\"" + name + "\">" + name_svg + "</h1>";
    html += "<p class=state id=state>&nbsp;</p></div></header>";
    html += BODY;
    html += std::string("<script>") + SCRIPT + "</script></main></html>";
    return html;
}

}  // namespace nestbox
