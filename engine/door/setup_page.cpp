// The phone's setup page, served on the pet's access point (the captive portal leads here). Self-contained: the
// phone has no internet while it is joined. Talks to /setup/* (door/setup_http.cpp).
#include "door/setup.h"

namespace nestbox {

const char* setup_page() {
    return R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<title>Set up your pet</title>
<style>
:root{--bg:#f4f1ea;--card:#fff;--ink:#1d2125;--soft:#5f6873;--line:#e4dfd4;--accent:#c4521c;--accent-ink:#fff;
--pick:#fff4ec;--ok:#1f8a4c;--warn:#b2461a;--warn-bg:#fdeee6;--focus:#2f6fec;--shadow:0 1px 2px rgba(30,25,15,.06),0 4px 16px rgba(30,25,15,.06)}
@media (prefers-color-scheme:dark){:root{--bg:#111316;--card:#1b1e23;--ink:#f2f0e6;--soft:#a3abb5;--line:#2c3037;
--accent:#f08a4b;--accent-ink:#1b1206;--pick:#2a211b;--ok:#6fd49a;--warn:#ffb08a;--warn-bg:#33201a;--focus:#7ab4ff;--shadow:none}}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:17px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
padding:max(20px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) max(28px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left))}
main{max-width:30rem;margin:0 auto}
header{text-align:center;margin:4px 0 20px}
.pet{width:88px;height:88px;image-rendering:pixelated;display:block;margin:0 auto 6px}
.pet.hop{animation:hop 1.1s ease-in-out infinite}
@keyframes hop{0%,100%{transform:translateY(0)}50%{transform:translateY(-6px)}}
@media (prefers-reduced-motion:reduce){.pet.hop{animation:none}}
h1{font-size:1.55rem;line-height:1.2;margin:0 0 6px;letter-spacing:-.01em;overflow-wrap:anywhere}
h2{font-size:1.05rem;margin:0 0 10px;overflow-wrap:anywhere}
p{margin:0 0 12px}
.sub{color:var(--soft);margin:0}
.card{background:var(--card);border-radius:16px;box-shadow:var(--shadow);border:1px solid var(--line);padding:6px;margin:0 0 14px}
.pad{padding:14px 12px}
.nets{list-style:none;margin:0;padding:0}
.net{display:flex;align-items:center;gap:12px;width:100%;min-height:52px;padding:10px 12px;border:0;border-radius:11px;
background:none;color:inherit;font:inherit;text-align:start;cursor:pointer}
.net+.net{margin-top:2px}
.net[aria-checked=true]{background:var(--pick);box-shadow:inset 0 0 0 2px var(--accent)}
.net .name{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:500;text-align:left;unicode-bidi:plaintext}
.net svg{flex:none;color:var(--soft)}
.net .tag{flex:none;color:var(--soft);font-size:14px}
.net[aria-checked=true] .tick{color:var(--accent)}
.tick{width:20px;height:20px;display:none}
.net[aria-checked=true] .tick{display:block}
.row{display:flex;gap:8px;flex-wrap:wrap;padding:6px 6px 4px}
.link{background:none;border:0;color:var(--accent);font:inherit;font-weight:600;padding:10px 8px;border-radius:9px;cursor:pointer;min-height:44px}
.empty{padding:16px 12px;color:var(--soft)}
label{display:block;font-weight:600;margin:0 0 6px;overflow-wrap:anywhere}
.field{display:flex;gap:8px;align-items:stretch}
input{width:100%;min-width:0;font:inherit;font-size:18px;color:var(--ink);background:var(--bg);border:1.5px solid var(--line);border-radius:11px;padding:12px 13px;min-height:50px}
input:focus{outline:none;border-color:var(--focus);box-shadow:0 0 0 3px color-mix(in srgb,var(--focus) 25%,transparent)}
.ghost{flex:none;background:var(--bg);border:1.5px solid var(--line);color:var(--ink);font:inherit;font-size:15px;font-weight:600;border-radius:11px;padding:0 14px;cursor:pointer;min-height:50px}
.hint{color:var(--soft);font-size:15px;margin:8px 0 0}
.bad{color:var(--warn);font-size:15px;margin:8px 0 0}
details{margin:12px 0 0}
summary{color:var(--accent);font-weight:600;cursor:pointer;padding:8px 0;min-height:44px;display:flex;align-items:center;list-style:none}
summary::-webkit-details-marker{display:none}
summary::after{content:"";width:7px;height:7px;border:solid currentColor;border-width:0 2px 2px 0;transform:rotate(45deg);margin:-3px 0 0 10px;transition:transform .15s}
details[open] summary::after{transform:rotate(-135deg);margin-top:4px}
details ul{margin:2px 0 4px;padding-inline-start:20px;color:var(--soft);font-size:15px}
details li{margin:6px 0}
.go{display:block;width:100%;min-height:54px;border:0;border-radius:14px;background:var(--accent);color:var(--accent-ink);font:inherit;font-size:18px;font-weight:700;cursor:pointer;margin:16px 0 0}
.go:disabled{opacity:.4;cursor:default}
.quiet{display:block;margin:6px auto 0;text-align:center}
.alert{background:var(--warn-bg);color:var(--warn);border-radius:12px;padding:12px 14px;margin:0 0 14px;font-weight:500}
.center{text-align:center}
.bar{height:8px;border-radius:8px;background:var(--line);overflow:hidden;margin:18px 4px 12px}
.bar i{display:block;height:100%;width:0;background:var(--accent);border-radius:8px;transition:width .6s linear}
.big{font-size:44px;line-height:1;margin:0 0 8px;color:var(--ok)}
.spin{width:22px;height:22px;border:3px solid var(--line);border-top-color:var(--accent);border-radius:50%;display:inline-block;animation:sp 0.9s linear infinite;vertical-align:-5px}
@keyframes sp{to{transform:rotate(360deg)}}
code{font:600 16px ui-monospace,Menlo,monospace;overflow-wrap:anywhere}
.addr{background:var(--bg);border-radius:10px;padding:10px 12px;margin:8px 0 0}
a{color:var(--accent)}
:focus-visible{outline:3px solid var(--focus);outline-offset:2px}
[hidden]{display:none!important}
</style>
</head>
<body>
<main>
<header>
  <img class="pet" id="pet" src="/setup/pet.svg" alt="">
  <h1 id="title" dir="auto">Let’s get your pet online</h1>
  <p class="sub" id="lead">Pick the Wi-Fi it will live on.</p>
</header>

<section id="pick">
  <div class="card">
    <ul class="nets" id="nets" role="radiogroup" aria-label="Wi-Fi networks"></ul>
    <div class="empty" id="looking"><span class="spin"></span>&nbsp; Looking for Wi-Fi…</div>
    <div class="empty" id="none" hidden>I don’t see any Wi-Fi I can use. I need a 2.4 GHz network.</div>
    <div class="row">
      <button class="link" id="again" type="button">Look again</button>
      <button class="link" id="other" type="button">Other network…</button>
    </div>
  </div>
  <form class="card pad" id="form" hidden novalidate>
    <div class="alert" id="err" role="alert" hidden></div>
    <div id="ssidbox" hidden>
      <label for="ssid">Network name</label>
      <input id="ssid" dir="auto" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" maxlength="32">
      <div style="height:14px"></div>
    </div>
    <div id="passbox">
      <label for="pass" id="passlabel" dir="auto">Password</label>
      <div class="field">
        <input id="pass" type="text" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" maxlength="64" enterkeyhint="go">
        <button class="ghost" id="show" type="button" aria-controls="pass">Hide</button>
      </div>
      <p class="bad" id="passbad" hidden>Wi-Fi passwords have at least 8 characters.</p>
      <details>
        <summary>Where’s my password?</summary>
        <ul>
          <li>On a sticker on your router, often called “Wi-Fi key” or “WPA key”.</li>
          <li>iPhone: Settings › Wi-Fi › ⓘ next to your network › Password, then tap Copy.</li>
          <li>Android: Settings › Wi-Fi › ⚙ next to your network › Share. The password is under the code.</li>
        </ul>
      </details>
    </div>
    <p class="hint" id="openhint" hidden>This network has no password.</p>
    <button class="go" id="connect" type="submit" disabled>Connect</button>
  </form>
  <button class="link quiet" id="nowifi" type="button">No Wi-Fi here? Just be a pet</button>
</section>

<section id="checking" class="card pad center" hidden aria-live="polite">
  <h2 id="checktitle" dir="auto">Checking…</h2>
  <div class="bar"><i id="fill"></i></div>
  <p class="sub" id="checknote">Stay on this page. It takes up to 20 seconds.</p>
</section>

<section id="done" hidden>
  <form class="card pad" id="finish">
    <label for="name">Give your pet a name <span class="sub" style="font-weight:400">(any language)</span></label>
    <input id="name" dir="auto" maxlength="40" autocomplete="off">
    <button class="go" type="submit" id="open"></button>
    <p class="hint center" id="opennote" dir="auto"></p>
  </form>
</section>

<section id="going" class="card pad center" hidden aria-live="polite">
  <p><span class="spin"></span></p>
  <h2 id="goingtitle" dir="auto"></h2>
  <p class="sub" id="goingnote"></p>
  <div id="fallback" hidden>
    <p class="sub" style="margin-top:14px">If nothing happens, join your Wi-Fi and open:</p>
    <div class="addr"><a id="addr1"><code id="addr1t"></code></a></div>
    <div class="addr" id="addr2box" hidden><a id="addr2"><code id="addr2t"></code></a></div>
  </div>
</section>

<section id="offline" class="card pad" hidden>
  <h2 id="offtitle" dir="auto"></h2>
  <p id="offtext" dir="auto"></p>
  <button class="go" id="offyes" type="button">Just be a pet</button>
  <button class="link quiet" id="offno" type="button">Back to Wi-Fi</button>
</section>

<section id="offdone" class="card pad center" hidden>
  <div class="big" aria-hidden="true">✓</div>
  <h2 id="offdonetitle" dir="auto"></h2>
  <p class="sub">You can close this page.</p>
</section>
</main>

<script>
(() => {
const $ = id => document.getElementById(id);
const views = ['pick', 'checking', 'done', 'going', 'offline', 'offdone'];
let pet = 'your pet', nets = [], chosen = null, custom = false, last = null, polling = false;

let again = false;
function show(v, head) {
  views.forEach(x => $(x).hidden = x !== v);
  $('pet').classList.toggle('hop', v === 'checking' || v === 'going');
  if (v === 'pick') head = again ? [`Let’s get ${pet} back online`, `Pick the Wi-Fi ${pet} will live on now.`]
                                 : [`Let’s get ${pet} online`, `Pick the Wi-Fi ${pet} will live on.`];
  $('title').textContent = head ? head[0] : pet;
  $('lead').hidden = !head;
  if (head) $('lead').textContent = head[1];
  window.scrollTo(0, 0);
}
function clock() {
  return {tz: (Intl.DateTimeFormat().resolvedOptions().timeZone || ''), time: Date.now(),
          utc_offset: -new Date().getTimezoneOffset() * 60};
}
async function api(path, body) {
  const r = await fetch('/setup/' + path, body === undefined ? {cache: 'no-store'} :
    {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || 'Something went wrong');
  return j;
}
function bars(n) {
  let s = '<svg width="20" height="16" viewBox="0 0 20 16" aria-hidden="true">';
  for (let i = 0; i < 4; i++) s += `<rect x="${i * 5 + 1}" y="${12 - i * 4}" width="3.4" height="${4 + i * 4}" rx="1" fill="currentColor" opacity="${i < n ? 1 : .22}"/>`;
  return s + '</svg>';
}
const LOCK = '<svg width="14" height="16" viewBox="0 0 14 16" aria-label="needs a password"><path d="M3.5 7V5a3.5 3.5 0 0 1 7 0v2" fill="none" stroke="currentColor" stroke-width="1.8"/><rect x="1" y="7" width="12" height="8.5" rx="2" fill="currentColor"/></svg>';
const TICK = '<svg class="tick" viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10.5l4 4 8-9" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/></svg>';

function drawNets() {
  const ul = $('nets');
  ul.innerHTML = '';
  nets.forEach(n => {
    const b = document.createElement('button');
    b.type = 'button'; b.className = 'net'; b.setAttribute('role', 'radio');
    b.setAttribute('aria-checked', !custom && chosen && chosen.ssid === n.ssid ? 'true' : 'false');
    b.innerHTML = '<span class="name" dir="auto"></span>' + (n.enterprise ? '<span class="tag">needs a login</span>' : '') +
      (n.secure ? LOCK : '') + bars(n.bars) + TICK;
    b.querySelector('.name').textContent = n.ssid;
    b.onclick = () => pick(n, false);
    const li = document.createElement('li'); li.appendChild(b); ul.appendChild(li);
  });
}
function pick(n, other) {
  chosen = n; custom = other;
  drawNets();
  $('form').hidden = false;
  $('ssidbox').hidden = !other;
  const secure = other || n.secure;
  $('passbox').hidden = !secure;
  $('openhint').hidden = secure;
  $('passlabel').textContent = other ? 'Password' : `Password for ${n.ssid}`;
  $('passbad').hidden = true;
  $('connect').disabled = false;
  if (n && n.enterprise) showError(`${n.ssid} needs a login ${pet} can’t do. A phone’s hotspot works (on iPhone, turn on Maximize Compatibility).`);
  else $('err').hidden = true;
  (other ? $('ssid') : secure ? $('pass') : $('connect')).focus({preventScroll: true});
  $('form').scrollIntoView({behavior: 'smooth', block: 'nearest'});
}
function showError(msg) { $('err').textContent = msg; $('err').hidden = false; }
// After a failed try: the form in view, with what was typed selected, ready to type over.
function retry() {
  if ($('form').hidden) return;
  $('form').scrollIntoView({block: 'start'});
  const f = !$('passbox').hidden ? $('pass') : !$('ssidbox').hidden ? $('ssid') : null;
  if (f) { f.focus({preventScroll: true}); f.select(); }
}

async function loadNets() {
  $('looking').hidden = false; $('none').hidden = true; $('again').disabled = true;
  for (let i = 0; i < 30; i++) {
    try {
      const j = await api('networks');
      pet = j.pet || pet; document.title = `Set up ${pet}`;
      if (!views.some(v => v !== 'pick' && !$(v).hidden)) show('pick');
      if (!j.scanning) { nets = j.networks || []; break; }
    } catch (e) {}
    await new Promise(r => setTimeout(r, 700));
  }
  $('looking').hidden = true; $('again').disabled = false;
  $('none').hidden = nets.length > 0;
  if (chosen && !custom && !nets.some(n => n.ssid === chosen.ssid)) { chosen = null; $('form').hidden = true; }
  drawNets();
}

$('again').onclick = async () => { try { await api('scan', {}); } catch (e) {} nets = []; drawNets(); loadNets(); };
$('other').onclick = () => pick({ssid: '', secure: true}, true);
$('show').onclick = () => {
  const p = $('pass'), hide = p.type === 'text';
  p.type = hide ? 'password' : 'text'; $('show').textContent = hide ? 'Show' : 'Hide';
};
$('form').onsubmit = async e => {
  e.preventDefault();
  const ssid = custom ? $('ssid').value.trim() : chosen.ssid;
  const password = (custom || chosen.secure) ? $('pass').value : '';
  if (!ssid) { $('ssid').focus(); return; }
  if (password && password.length < 8) { $('passbad').hidden = false; $('pass').focus(); return; }
  if (!custom && chosen.secure && !password) { $('passbad').hidden = false; $('pass').focus(); return; }
  $('err').hidden = true;
  try { await api('connect', Object.assign({ssid, password}, clock())); }
  catch (err) { showError(err.message); return; }
  last = ssid;
  watch();
};
$('nowifi').onclick = () => show('offline');
$('offno').onclick = () => show('pick');
$('offyes').onclick = async () => {
  try { await api('offline', clock()); show('offdone'); } catch (e) { showError(e.message); show('pick'); }
};
$('finish').onsubmit = async e => {
  e.preventDefault();
  $('open').disabled = true;
  let j;
  try { j = await api('finish', {name: $('name').value.trim()}); }
  catch (err) { $('open').disabled = false; $('opennote').textContent = err.message; return; }
  const name = $('name').value.trim() || pet;
  show('going');
  $('goingtitle').textContent = `Taking you to ${name}’s page…`;
  $('goingnote').textContent = `Your phone goes back to ${(status && status.ssid) || last || 'your Wi-Fi'}.`;
  $('addr1').href = j.url; $('addr1t').textContent = j.url.replace(/\/welcome\?.*/, '/');
  if (status && status.host) { $('addr2box').hidden = false; $('addr2').href = 'http://' + status.host + '/'; $('addr2t').textContent = 'http://' + status.host + '/'; }
  setTimeout(() => $('fallback').hidden = false, 15000);
  const tryHome = () => {
    const img = new Image();
    img.onload = () => { location.href = j.url; };
    img.onerror = () => setTimeout(tryHome, 1500);
    img.src = j.ping + '?n=' + Date.now();
  };
  setTimeout(tryHome, 2500);
};

let status = null;
function render(s) {
  status = s;
  pet = s.pet || pet;
  if (s.phase === 'checking') {
    show('checking');
    $('checktitle').textContent = `Checking ${s.ssid}…`;
    $('fill').style.width = Math.min(100, 100 * s.elapsed_ms / s.limit_ms) + '%';
    $('checknote').textContent = 'Stay on this page. It takes up to 20 seconds.';
  } else if (s.phase === 'failed') {
    show('pick'); showError(s.reason); retry();
  } else if (s.phase === 'allset' || (s.ok && s.phase === 'off')) {
    if (!$('done').hidden || !$('going').hidden) return;
    show('done', [`${pet} is on ${s.ssid}!`, 'It saved this Wi-Fi and finds it again by itself.']);
    last = s.ssid;
    if (!$('name').value) $('name').value = pet;
    $('open').textContent = `Open ${pet}’s page`;
    $('opennote').textContent = `Your phone goes back to ${s.ssid}, and the page opens by itself.`;
  } else if (s.offline) {
    show('offdone');
  }
}
async function watch() {
  if (polling) return;
  polling = true;
  let lost = 0;
  while (true) {
    try {
      const s = await api('status'); lost = 0;
      render(s);
      if (s.phase !== 'checking') break;
    } catch (e) {
      if (++lost > 60) { showError(`Lost touch with ${pet}. Rejoin its Wi-Fi and reload this page.`); show('pick'); break; }
      $('checknote').textContent = `Reconnecting to ${pet}…`;
    }
    await new Promise(r => setTimeout(r, 750));
  }
  polling = false;
}

$('offtitle').textContent = 'Just be a pet?';
api('status').then(s => {
  pet = s.pet || pet;
  again = !!s.again;
  $('offtitle').textContent = `Keep ${pet} offline?`;
  $('offtext').textContent = `${pet} still plays, naps and keeps the time, but can’t react to your computer or anything else. To add Wi-Fi later, push a paper clip into the B hole on top and hold it for 5 seconds.`;
  $('offdonetitle').textContent = `${pet} is just a pet now`;
  if (s.phase === 'checking') watch(); else render(s);
  if ($('pick').hidden === false || s.phase === 'failed') show('pick');
  if (s.phase === 'failed') showError(s.reason);
}).catch(() => {});
loadNets();
})();
</script>
</body>
</html>
)PAGE";
}

}  // namespace nestbox
