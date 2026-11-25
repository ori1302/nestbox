#include "door/page.h"
#include <cstdio>
#include "door/pet_svg.h"
#include "screen/font.h"

namespace nestbox {

namespace {

// The docs' bench colours (the product's own), the goose's beak for what is on or chosen, the system's type. The
// device's own voice, the screen's pixel font, names the sections and reads out its flash, on a gauge in the bezel's
// navy; the pets show as themselves (GET /v1/pets/NAME/picture), drawn at whole device pixels.
const char* const STYLE = R"CSS(
:root{--bg:#e3e4e0;--raised:#eeefeb;--line:#c9ccc7;--ink:#1d2433;--soft:#596071;--beak:#f39c34;--bezel:#1d2433;
--edge:#1d2433;--lcd:#ebe7dc;--lcd-soft:#98a0b3;--warn:#a3341f;color-scheme:light}
@media(prefers-color-scheme:dark){:root{--bg:#16181a;--raised:#1e2124;--line:#33383d;--ink:#e8eaeb;--soft:#a2a9ae;
--edge:#34405a;--warn:#ff8f73;color-scheme:dark}}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif;
-webkit-text-size-adjust:100%}
main{max-width:31rem;margin:0 auto;padding:28px max(16px,env(safe-area-inset-right)) 48px max(16px,env(safe-area-inset-left))}
button{font:inherit;color:inherit;background:none;border:0;padding:0;cursor:pointer;text-align:left}
button:disabled{cursor:default}
:focus-visible{outline:3px solid var(--beak);outline-offset:2px}
svg,img{display:block}.px{image-rendering:pixelated}
.vh{position:absolute;width:1px;height:1px;overflow:hidden;clip-path:inset(50%);white-space:nowrap}
.hero{display:flex;align-items:center;gap:16px}
.glass{flex:none;width:88px;height:88px;border-radius:20px;background:var(--bezel);box-shadow:inset 0 0 0 1px var(--edge);
display:grid;place-items:center;transition:filter .6s}
.glass[data-s=dim]{filter:brightness(.62)}.glass[data-s=off] img{opacity:0}
.who{min-width:0}
h1{margin:0;font-size:1.9rem;line-height:1.1;overflow-wrap:anywhere}
h1 svg{height:2.1rem;width:auto;max-width:100%}
.state{margin:6px 0 0;color:var(--soft)}
.lock{display:flex;align-items:center;gap:16px;background:var(--ink);color:var(--bg);border-radius:12px;
padding:12px 12px 12px 16px;margin:24px 0 0}
.lock p{margin:0;flex:1}.lock b{font-variant-numeric:tabular-nums}
.main{flex:none;min-height:44px;padding:0 18px;border-radius:999px;background:var(--bg);color:var(--ink);font-weight:600}
section{margin-top:40px}
.head{display:flex;align-items:center;justify-content:space-between;min-height:44px;margin:0 0 8px}
h2{margin:0}
.meta{color:var(--soft);font-size:15px}
.txt{min-height:44px;padding:0 4px;color:var(--soft);font-size:15px;font-weight:500}
.txt[aria-pressed=true]{color:var(--ink);font-weight:600}
.panel{background:var(--raised);border-radius:12px;box-shadow:inset 0 0 0 1px var(--line);padding:0 16px}
.pets{list-style:none;margin:0;padding:0}.pets>li+li{border-top:1px solid var(--line)}
.pet{display:flex;align-items:center;gap:12px;width:100%;min-height:60px;padding:8px 0}
.pic{flex:none;width:40px;height:44px;display:flex;align-items:flex-end;justify-content:center}
.nm{font-weight:500;overflow-wrap:anywhere;min-width:0}
.kb{flex:1;color:var(--soft);font-size:14px;font-variant-numeric:tabular-nums;white-space:nowrap}
.act{color:var(--soft);font-size:15px;white-space:nowrap}
button.pet:hover .act{color:var(--ink)}button.pet:disabled .act{visibility:hidden}
.out .pic{box-shadow:0 2px 0 var(--beak)}.out .act,.coming .act{color:var(--ink);font-weight:600}
.pet .warn{color:var(--warn);font-weight:500}
.add{border-top:1px solid var(--line);min-height:56px}.add .pic{height:20px}.add .nm{font-weight:600}
.add:disabled{opacity:.45}
.gauge{margin:4px -4px 12px;background:var(--bezel);color:var(--lcd);border-radius:8px;padding:12px;
box-shadow:inset 0 0 0 1px var(--edge)}
.gauge .top{align-items:flex-end;margin:0}
.free{color:var(--lcd-soft);font-size:14px;line-height:1}
.blocks{display:flex;gap:2px;height:14px;margin-top:10px}
.blocks i{flex:1 1 0;min-width:2px;background:var(--lcd);opacity:.55}.blocks i:nth-child(even){opacity:.8}
.blocks .on{background:var(--beak);opacity:1}.blocks .left{background:var(--lcd-soft);opacity:.18}
.ask{list-style:none;background:var(--bg);border-radius:12px;padding:12px;margin:0 0 10px}
.ask p{margin:0 0 10px}.ask div{display:flex;gap:8px;flex-wrap:wrap}
.ask button{min-height:40px;padding:0 16px;border-radius:999px;font-weight:600;border:1.5px solid var(--ink)}
.ask .go{background:var(--ink);color:var(--bg)}.ask .warn{border-color:var(--warn)}
.ask .quiet{border-color:transparent;color:var(--soft)}
.ctl{padding:16px 0}.ctl+.ctl{border-top:1px solid var(--line)}
.top{display:flex;justify-content:space-between;align-items:center;gap:12px;margin:0 0 8px}.top.line{margin:0}
.ctl label,.lb{font-weight:600}
output{color:var(--soft);font-variant-numeric:tabular-nums}
input:disabled{opacity:.45}
input[type=range]{-webkit-appearance:none;appearance:none;display:block;width:100%;margin:0;height:36px;background:none}
input[type=range]::-webkit-slider-runnable-track{height:4px;border-radius:2px;background:var(--line)}
input[type=range]::-moz-range-track{height:4px;border-radius:2px;background:var(--line)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;margin-top:-10px;border-radius:6px;
background:var(--beak)}
input[type=range]::-moz-range-thumb{width:24px;height:24px;border:0;border-radius:6px;background:var(--beak)}
.seg{display:grid;grid-template-columns:repeat(6,1fr);gap:4px}
.seg button{min-height:44px;border-radius:6px;background:var(--bg);color:var(--soft);font-size:15px;text-align:center;
font-variant-numeric:tabular-nums;white-space:nowrap}
.seg button[aria-pressed=true]{background:var(--beak);color:#1d2433;font-weight:600}
.seg button:disabled{opacity:.45}
.seg.two{grid-template-columns:repeat(2,64px)}
.times{display:flex;align-items:center;gap:8px;margin-top:12px}.times label{font-weight:400;color:var(--soft)}
input[type=time]{font:inherit;font-variant-numeric:tabular-nums;color:var(--ink);background:var(--bg);border:0;
border-radius:6px;padding:0 12px;min-height:44px}
.hint{margin:8px 0 0;color:var(--soft);font-size:14px;max-width:34em}
.say{margin:8px 2px 0;color:var(--soft);font-size:15px}.say:empty{display:none}
.list{margin:0;border-top:1px solid var(--line)}
.list>*{display:flex;align-items:center;gap:12px;min-height:52px;padding:12px 0;border-bottom:1px solid var(--line)}
dt{color:var(--soft);flex:1}dd{margin:0;text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.acts{display:flex;border-bottom:1px solid var(--line)}
.do{flex:1;min-height:52px;font-weight:600}.do.warn{text-align:right}.do:disabled{opacity:.45}
.warn{color:var(--warn)}
a.item{display:block;text-decoration:none;color:inherit}
.item b{display:block;font-weight:600}.item span{display:block;color:var(--soft);font-size:14px}
.grow{flex:1;min-width:0}
code{display:block;margin-top:4px;font:500 14px ui-monospace,Menlo,monospace;overflow-wrap:anywhere}
footer{margin-top:24px}footer button{min-height:44px;color:var(--soft);font-size:15px;text-decoration:underline;
text-underline-offset:3px}
@media(max-width:400px){.seg{grid-template-columns:repeat(3,1fr)}.seg.two{grid-template-columns:repeat(2,56px)}}
@media(prefers-reduced-motion:reduce){*{transition:none!important}}
)CSS";

const char* const BODY = R"HTML(
<div class=lock id=lock hidden><p id=lock-text>You can look around. To change anything, allow this browser once.</p>
<button class=main id=allow type=button>Allow changes</button></div>
<section><div class=head>{{Pets}}<button class=txt id=edit type=button aria-pressed=false>Edit</button></div>
<div class=panel><ul class=pets id=pets></ul>
<div class=gauge id=room role=img hidden><div class=top><span id=used></span><span class=free id=free></span></div>
<div class=blocks id=bar></div></div>
<button class="pet add" id=add type=button><span class=pic></span><span class=nm>Add a pet&hellip;</span></button>
<input id=egg type=file accept=".egg" hidden></div>
<p class=say id=pets-say role=status></p></section>
<section><div class=head>{{Screen}}<span class=meta id=now-state></span></div>
<div class=panel id=screen>
<div class=ctl><div class=top><label for=bright>Brightness</label><output id=bright-v></output></div>
<input type=range id=bright min=5 max=100 step=5></div>
<div class=ctl><div class=top><span class=lb id=l-dim_after>Dim after</span></div>
<div class=seg id=dim_after role=group aria-labelledby=l-dim_after data-v="5 15 30 60 120 0"
data-l="5 min|15 min|30 min|1 hr|2 hr|Never"></div></div>
<div class=ctl><div class=top><label for=dim>Dimmed brightness</label><output id=dim-v></output></div>
<input type=range id=dim min=5 max=100 step=5></div>
<div class=ctl><div class=top><span class=lb id=l-off_after>Turn off after</span></div>
<div class=seg id=off_after role=group aria-labelledby=l-off_after data-v="15 30 60 180 360 0"
data-l="15 min|30 min|1 hr|3 hr|6 hr|Never"></div>
<p class=hint>A press on a dark screen only wakes it.</p></div>
<div class=ctl><div class="top line"><span class=lb id=l-night>Night hours</span>
<div class="seg two" id=night role=group aria-labelledby=l-night data-v="1 0" data-l="On|Off"></div></div>
<div class=times id=times hidden><label for=from>From</label><input type=time id=from>
<label for=to>to</label><input type=time id=to></div>
<p class=hint>At night the screen dims after a minute and turns off after ten.</p></div>
<div class=ctl><div class="top line"><span class=lb id=l-care>Burn-in care</span>
<div class="seg two" id=care role=group aria-labelledby=l-care data-v="1 0" data-l="On|Off"></div></div>
<p class=hint id=care-hint></p></div>
</div>
<p class=say id=screen-say role=status></p></section>
<section><div class=head>{{This NestBox}}</div>
<dl class=list id=facts></dl><div id=dev-ask></div>
<div class=acts><button class=do id=restart type=button>Restart</button>
<button class="do warn" id=forget type=button>Forget Wi-Fi</button></div>
<p class=say id=dev-say role=status></p></section>
<section><div class=head>{{Connect}}</div>
<div class=list>
<a class=item href="https://nestbox.pet"><b>NestBox for Mac</b><span>Lets the pet follow your typing and your Mac's
sleep.</span></a>
<div><div class="item grow"><b>Coding agents</b><span>Give your agent this address and it sets itself up:</span>
<code id=agents></code></div><button class=txt id=copy type=button>Copy</button></div>
<a class=item href="/api"><b>HTTP API</b><span>Every route, with examples.</span></a>
</div></section>
<footer id=foot></footer>
)HTML";

const char* const SCRIPT = R"JS(
const $ = id => document.getElementById(id), KEY = 'nestbox-key', G = {{GLYPHS}};
let key = '', dev = null, busy = false, pets = null, editing = false, coming = '', hero = '';
try { key = localStorage.getItem(KEY) || ''; } catch (e) {}
const keep = k => { key = k; try { k ? localStorage.setItem(KEY, k) : localStorage.removeItem(KEY); } catch (e) {} };
const say = (id, t) => { $(id).textContent = t || ''; };
const wait = ms => new Promise(r => setTimeout(r, ms));
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text) e.textContent = text; return e; };
const auth = () => key ? {Authorization: 'Bearer ' + key} : {};
async function api(method, path, body) {
  if (body && !(body instanceof Blob)) body = JSON.stringify(body);
  const r = await fetch(path, {method, body, headers: auth(), cache: 'no-store'});
  let j = {};
  try { j = await r.json(); } catch (e) {}
  if (r.status === 401) { if (key) keep(''); render(); throw new Error('Allow changes from this browser first.'); }
  if (!r.ok) throw new Error(j.error || 'NestBox answered ' + r.status);
  return j;
}
// Words in the screen's font, two CSS pixels to its pixel.
function pix(t) {
  let pen = 0, r = '';
  for (const c of t) {
    const g = G[c]; if (!g) continue;
    const [a, x, y, w, h, bits] = g, rb = (w + 7) >> 3;
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++)
      if (parseInt(bits.substr((j * rb + (i >> 3)) * 2, 2), 16) >> (i & 7) & 1) r += `<rect x="${pen + x + i}" y="${y + j}" width="1" height="1"/>`;
    pen += a;
  }
  const s = el('span');
  s.innerHTML = `<svg viewBox="0 0 ${pen} ${G.h}" width="${pen * 2}" height="${G.h * 2}" shape-rendering="crispEdges" fill="currentColor" aria-hidden="true">${r}</svg>`;
  return s;
}
// A pet's picture fitted to a box, by whole device pixels where that keeps it near the box's size.
const faces = {};
function face(name, stamp, bw, bh) {
  const img = el('img', 'px'), k = name + '/' + stamp;
  img.alt = '';
  const show = u => {
    img.onload = () => {
      const d = devicePixelRatio || 1, w = img.naturalWidth, h = img.naturalHeight, fit = Math.min(bw / w, bh / h);
      const whole = Math.floor(fit * d) / d, s = whole >= fit * .75 ? whole : fit;   // whole device pixels unless too small
      img.width = w * s; img.height = h * s;
    };
    img.src = u;
  };
  if (faces[k]) show(faces[k]);
  else fetch('/v1/pets/' + name + '/picture', {headers: auth()}).then(r => r.ok ? r.blob() : null)
    .then(b => { if (b) show(faces[k] = URL.createObjectURL(b)); }, () => {});
  return img;
}
const MB = 1048576, size = b => b >= MB * .95 ? (b / MB).toFixed(1) + ' MB' : Math.max(1, Math.round(b / 1024)) + ' KB';
const STATE = {awake: 'Awake', dim: 'Dimmed', off: 'Off'};
function render() {
  const can = !!key;
  $('lock').hidden = can;
  document.querySelectorAll('#screen input, #screen .seg button, #add, #edit, #restart, #forget, #pets button')
    .forEach(b => { b.disabled = !can; });
  $('foot').replaceChildren();
  if (can) {
    const f = el('button', '', 'Stop allowing changes here'); f.type = 'button';
    f.onclick = () => { keep(''); render(); }; $('foot').append(f);
  }
}
function seg(id, value, send) {
  const e = $(id), vals = e.dataset.v.split(' ').map(Number), labels = e.dataset.l.split('|');
  if (!e.children.length) vals.forEach((v, i) => {
    const b = el('button', '', labels[i]); b.type = 'button'; b.dataset.v = v;
    b.onclick = () => { seg(id, v); send(v); }; e.append(b);
  });
  [...e.children].forEach(b => b.setAttribute('aria-pressed', String(+b.dataset.v === value)));
}
const hhmm = t => t === 'off' ? null : t.split('-');
function show(j) {
  dev = j.device;
  const s = dev.screen, pet = pets && pets.pets.find(p => p.name === j.now.pet);
  const name = pet ? pet.title || pet.name : j.now.pet;
  $('glass').dataset.s = s.state;
  const stamp = pet ? pet.bytes : '';
  if (hero !== j.now.pet + stamp) { hero = j.now.pet + stamp; $('glass').replaceChildren(face(j.now.pet, stamp, 64, 64)); }
  $('state').textContent = name + ' is out. The screen is ' + (s.state === 'awake' ? 'awake' : s.state === 'dim' ?
    'dimmed' : 'off') + (s.state === 'off' ? '.' : ', at ' + s.backlight + '\u00a0%.');
  $('now-state').textContent = STATE[s.state];
  if (document.activeElement !== $('bright')) { $('bright').value = s.bright; $('bright-v').value = s.bright + ' %'; }
  if (document.activeElement !== $('dim')) { $('dim').value = s.dim; $('dim-v').value = s.dim + ' %'; }
  seg('dim_after', s.dim_after, v => set({dim_after: v}));
  seg('off_after', s.off_after, v => set({off_after: v}));
  seg('care', +s.care, v => set({care: !!v}));
  $('care-hint').textContent = s.oled ?
    'Moves the picture a pixel every few minutes, and lets the status bar and gauges go while they stay the same, ' +
    'so nothing burns into this OLED screen.' :
    'Lets the status bar and gauges go while they stay the same. Made for OLED screens; this one is LCD.';
  const n = hhmm(s.night);
  if (!$('times').contains(document.activeElement)) {
    seg('night', +!!n, night); $('times').hidden = !n;
    if (n) { $('from').value = n[0]; $('to').value = n[1]; }
  }
  // Its name stays when the router hands out another IP address; the IP is shown only as the way this page came.
  const facts = [['Address', dev.name + '.local']];
  if (/^[0-9.]+$|^\[/.test(location.hostname)) facts.push(['IP address', location.hostname]);
  if (dev.temperature_c !== undefined) facts.push(['Temperature', dev.temperature_c + ' °C']);
  if (dev.wifi) facts.push(['Wi-Fi signal', dev.wifi[0].toUpperCase() + dev.wifi.slice(1)]);
  if (j.uptime_s !== undefined) facts.push(['Running for', since(j.uptime_s)]);
  facts.push(['Firmware', dev.firmware]);
  $('facts').replaceChildren(...facts.map(([k, v]) => { const r = el('div'); r.append(el('dt', '', k), el('dd', '', v)); return r; }));
  $('agents').textContent = dev.agents || 'http://' + dev.name + '.local/agents';
  render();
}
function since(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
  return d ? d + (d === 1 ? ' day' : ' days') + (h ? ', ' + h + ' h' : '') : h ? h + ' h ' + m + ' min' : m + ' min';
}
async function refresh() {
  try { show(await api('GET', '/v1')); } catch (e) { $('state').textContent = e.message; }
}
// Opened by its IP address (setup's handover, a typed address) and not yet allowed here: moves to its name, which
// stays when the router hands out another address, as soon as this browser can reach the name.
function toName() {
  if (key || !dev || !/^[0-9.]+$/.test(location.hostname)) return;
  const img = new Image(), host = dev.name + '.local' + (location.port ? ':' + location.port : '');
  img.onload = () => location.replace('http://' + host + '/');
  img.src = 'http://' + host + '/ping.png?' + Date.now();
}
async function set(screen) {
  try {
    const j = await api('POST', '/v1', {screen});
    say('screen-say', (j.warnings && j.warnings[0]) || 'Saved.');
    await refresh();
  } catch (e) { say('screen-say', e.message); }
}
function petRow(p) {
  const li = el('li'), t = p.title || p.name, out = p.name === pets.out, take = editing && pets.pets.length > 1;
  const b = el(out && !take ? 'div' : 'button', 'pet' + (out ? ' out' : p.name === coming ? ' coming' : ''));
  const pic = el('span', 'pic'), act = el('span', 'act');
  pic.append(face(p.name, p.bytes, 40, 44));
  b.append(pic, el('span', 'nm', t), el('span', 'kb', p.bytes ? size(p.bytes) : ''), act);
  if (take) {
    b.type = 'button'; act.textContent = 'Take off'; act.classList.add('warn'); b.setAttribute('aria-label', 'Take ' + t + ' off');
    b.onclick = () => confirmRow(li, 'Take ' + t + ' off this NestBox? Its file is deleted from it.', 'Take off',
      () => act_(async () => { await api('DELETE', '/v1/pets/' + p.name); return watch(); }), true);
  } else if (out) {
    act.textContent = 'Out now';
  } else {
    b.type = 'button'; b.setAttribute('aria-label', 'Bring out ' + t);
    act.textContent = p.name === coming ? 'Coming' + String.fromCharCode(8230) : 'Bring out';
    b.onclick = () => bringOut(p.name);
  }
  li.append(b);
  return li;
}
function confirmRow(after, text, yes, run, warn) {
  const a = el(after.tagName === 'LI' ? 'li' : 'div', 'ask'), acts = el('div');
  const y = el('button', warn ? 'warn' : 'go', yes), n = el('button', 'quiet', 'Keep');
  y.type = n.type = 'button'; y.onclick = () => { a.remove(); run(); }; n.onclick = () => a.remove();
  acts.append(y, n); a.append(el('p', '', text), acts);
  document.querySelectorAll('.ask').forEach(x => x.remove());
  after.after(a); y.focus();
}
function room() {
  const s = pets.storage;
  $('room').hidden = !s;
  if (!s) return;
  const block = (cls, bytes, title) => { const i = el('i', cls); i.style.flexGrow = bytes; if (title) i.title = title; return i; };
  $('bar').replaceChildren(...pets.pets.filter(p => p.bytes).map(p =>
    block(p.name === pets.out ? 'on' : '', p.bytes, (p.title || p.name) + ', ' + size(p.bytes))),
    block('left', Math.max(0, s.total - s.used)));
  $('room').setAttribute('aria-label', size(s.used) + ' of ' + size(s.total) + ' used, ' + size(s.total - s.used) + ' free');
  $('used').replaceChildren(pix(size(s.used) + ' of ' + size(s.total)));
  $('free').textContent = size(s.total - s.used) + ' free';
}
function drawPets() { $('pets').replaceChildren(...pets.pets.map(petRow)); room(); render(); }
async function loadPets() {
  try { pets = await api('GET', '/v1/pets'); drawPets(); return pets; }
  catch (e) { say('pets-say', e.message); return null; }
}
async function act_(run) {
  if (busy) return;
  busy = true;
  try { say('pets-say', await run()); } catch (e) { say('pets-say', e.message); }
  busy = false; coming = '';
  await loadPets(); refresh();
}
const notDone = t => (t || '').replace(/^[a-z0-9-]+: /, 'Not done: ');
function bringOut(name) {
  act_(async () => {
    await api('POST', '/v1', {pet: name});
    coming = name; drawPets();
    for (let i = 0; i < 24 && pets.out !== name; i++) { await wait(500); if (!await loadPets()) break; }
    return pets.out === name ? '' : notDone(pets.last_change);
  });
}
async function watch() {
  for (;;) {
    const j = await loadPets();
    if (!j || !j.change) {
      const last = (j && j.last_change) || '';
      return last.startsWith('added ') ? 'Added.' : last.startsWith('took off ') ? 'Taken off.' : notDone(last);
    }
    say('pets-say', 'Saving' + String.fromCharCode(8230));
    await wait(1000);
  }
}
$('edit').onclick = () => {
  editing = !editing;
  $('edit').textContent = editing ? 'Done' : 'Edit'; $('edit').setAttribute('aria-pressed', String(editing));
  drawPets();
};
$('add').onclick = () => $('egg').click();
$('egg').onchange = () => {
  const f = $('egg').files[0]; $('egg').value = '';
  if (!f) return;
  const id = f.name.replace(/\.egg$/i, '').toLowerCase().replace(/[^a-z0-9-]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 32);
  if (!/\.egg$/i.test(f.name) || !id) return say('pets-say', 'Choose a pet’s .egg file.');
  const s = pets && pets.storage;
  if (s && f.size > s.total - s.used)
    return say('pets-say', 'Not enough room: this pet needs ' + size(f.size) + ' and ' + size(s.total - s.used) +
      ' is free. Take a pet off first.');
  say('pets-say', 'Sending' + String.fromCharCode(8230));
  act_(async () => { await api('PUT', '/v1/pets/' + id, f); return watch(); });
};
$('bright').oninput = () => { $('bright-v').value = $('bright').value + ' %'; };
$('dim').oninput = () => { $('dim-v').value = $('dim').value + ' %'; };
$('bright').onchange = () => set({bright: +$('bright').value});
$('dim').onchange = () => set({dim: +$('dim').value});
function night(on) {
  $('times').hidden = !on;
  set({night: on ? ($('from').value || '23:00') + '-' + ($('to').value || '07:00') : 'off'});
}
$('from').onchange = $('to').onchange = () => night(true);
$('allow').onclick = async () => {
  $('allow').disabled = true;
  try {
    for (;;) {
      const j = await api('POST', '/v1/pair', {name: 'this browser'});
      if (j.key) { keep(j.key); break; }
      $('lock-text').innerHTML = 'Press the button on top of your NestBox. <b>' + j.ends_in + ' s</b>';
      await wait(1000);
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
loadPets().then(refresh).then(toName);
setInterval(() => { if (!document.hidden) refresh(); }, 5000);
)JS";

// The glyphs the page draws itself (its flash readout), from the screen's smallest font: {char: [advance, x, y, w, h,
// rows in hex]} and "h", the line's height.
std::string glyph_table() {
    const FontSize& f = font_size(1);
    std::string js = "{";
    for (const char* c = "0123456789. ofKMB"; *c; c++) {
        const char* at = c;
        const Glyph* g = next_glyph(f, at);
        if (!g) continue;
        std::string hex;
        char buf[64];
        for (int i = 0; i < (g->w + 7) / 8 * g->h; i++) {
            std::snprintf(buf, sizeof buf, "%02x", f.bits[g->bits + i]);
            hex += buf;
        }
        std::snprintf(buf, sizeof buf, "\"%c\":[%d,%d,%d,%d,%d,\"", *c, g->advance, g->x, g->y, g->w, g->h);
        js += buf + hex + "\"],";
    }
    return js + "\"h\":" + std::to_string(f.cap + f.below) + "}";
}

// A section's heading in the screen's font, read out as its words.
std::string heading(const std::string& text) {
    const std::string svg = text_svg(text, 2, 2);
    return svg.empty() ? "<h2>" + text + "</h2>" : "<h2><span class=vh>" + text + "</span>" + svg + "</h2>";
}

void replace_first(std::string& s, const std::string& from, const std::string& to) {
    const size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
}

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

std::string device_page(const std::string& raw_name, const std::string& name_svg) {
    const std::string name = html_escape(raw_name);
    std::string html = "<!doctype html><html lang=en><meta charset=utf-8>"
                       "<meta name=viewport content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
                       "<meta name=color-scheme content=\"light dark\"><title>" + name + "</title><style>" + STYLE +
                       "</style><main><header class=hero><div class=glass id=glass></div><div class=who>";
    if (name_svg.empty()) html += "<h1 dir=auto>" + name + "</h1>";
    else html += "<h1 aria-label=\"" + name + "\">" + name_svg + "</h1>";
    html += "<p class=state id=state>&nbsp;</p></div></header>";
    std::string body = BODY, script = SCRIPT;
    for (const char* title : {"Pets", "Screen", "This NestBox", "Connect"})
        replace_first(body, std::string("{{") + title + "}}", heading(title));
    replace_first(script, "{{GLYPHS}}", glyph_table());
    html += body + "<script>" + script + "</script></main></html>";
    return html;
}

}  // namespace nestbox
