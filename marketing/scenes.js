// Writes the 3D marketing scenes (3840x2160) as html pages. The live themed UI is embedded in iframes
// so every pixel stays vector-crisp at render size. usage: node scenes.js
const fs = require('fs'), path = require('path');
const T = JSON.parse(fs.readFileSync(path.join(__dirname, 'themes.json'), 'utf8'));
const R = JSON.parse(fs.readFileSync(path.join(__dirname, 'rects.json'), 'utf8'));
const z = R.app[2] / 900;                       // rects were measured with the page scaled to fit
const rect = k => R[k].map(v => v / z);
fs.mkdirSync(path.join(__dirname, 'scenes'), { recursive: true });
const png = (id, v) => `../ui/${id}-${v}.png`;
const bg = (id, v, W, H, x, y) => `background:url(${png(id, v)}) ${-x}px ${-y}px/${W}px ${H}px no-repeat`;
const src = (id, v) => `../src/ui-${id}-${v}.html`;

const BASE = (t, body, extraCss = '') => `<!doctype html><html><head><meta charset="utf-8"><title>${t.name}</title><style>
*{box-sizing:border-box}
html,body{margin:0;width:3840px;height:2160px;overflow:hidden;background:#05060a;font-family:'Inter Tight','Segoe UI Variable Display','Segoe UI',system-ui,sans-serif;color:#eceef2}
.bg{position:absolute;inset:0;background:
  radial-gradient(ellipse 55% 60% at 66% 50%,${t.glow}55,transparent 70%),
  radial-gradient(ellipse 40% 45% at 8% 105%,${t.glow}33,transparent 70%),
  radial-gradient(ellipse 70% 40% at 50% -10%,#ffffff10,transparent 70%),#05060a}
.vig{position:absolute;inset:0;background:radial-gradient(ellipse 80% 80% at 50% 50%,transparent 55%,#000000aa 100%);pointer-events:none}
.grain{position:absolute;inset:0;opacity:.06;background-image:url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='200' height='200'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='.9' numOctaves='2'/></filter><rect width='200' height='200' filter='url(%23n)'/></svg>");pointer-events:none}
.txt{position:absolute;left:210px;z-index:5}
.eyebrow{font-size:44px;letter-spacing:.42em;color:${t.b};font-weight:600}
h1{margin:26px 0 0;font-size:250px;line-height:.92;font-weight:700;letter-spacing:-.025em}
h1 em{font-style:normal;color:${t.a};text-shadow:0 0 90px ${t.a}88}
.lead{margin-top:48px;font-size:58px;line-height:1.35;color:#9aa1ad;max-width:1000px;font-weight:500}
.chips{display:flex;flex-wrap:wrap;gap:18px;margin-top:56px;max-width:1100px}
.chips span{font-size:34px;padding:14px 30px;border-radius:999px;border:2px solid ${t.a}66;background:${t.a}1c;color:${t.b};font-weight:600}
.tag{position:absolute;right:200px;bottom:130px;z-index:5;font-size:40px;letter-spacing:.3em;color:#6b7280}
.tag b{color:${t.b};font-weight:600}
.scene{position:absolute;inset:0;perspective:3400px}
.p3{position:absolute;transform-style:preserve-3d}
iframe{border:0;display:block;pointer-events:none;background:#060708}
${extraCss}</style></head><body><div class="bg"></div>${body}<div class="vig"></div><div class="grain"></div></body></html>`;

const TEXT = (t, small) => `<div class="txt" style="top:${small ? 300 : 560}px"><div class="eyebrow">SELEVEN AUDIO</div>
<h1 style="${small ? 'font-size:200px' : ''}">Chromatic<br><em>Sampler</em></h1>
<div class="lead">Drop in any sound. It finds the pitch and plays it chromatically across the keyboard.</div>
<div class="chips"><span>VST3</span><span>Auto root detection</span><span>Reverb</span><span>Delay</span><span>Saturation</span><span>Filter</span></div></div>
<div class="tag">${t.name.toUpperCase()} · <b>CHROMATIC SAMPLER</b></div>`;

// full window floating in space
function hero(id, t) {
  const W = 2260, H = W * 600 / 900;
  const edges = Array.from({ length: 9 }, (_, i) => `<div class="p3" style="inset:0;border-radius:34px;background:${i < 8 ? '#0b0e13' : t.a};transform:translateZ(${-(i + 1) * 3}px)"></div>`).join('');
  return BASE(t, `<div class="scene" style="perspective-origin:62% 50%">
  <div class="p3" style="left:1330px;top:330px;width:${W}px;height:${H}px;transform:rotateX(13deg) rotateY(-27deg) rotateZ(1.6deg)">
    <div style="position:absolute;left:5%;right:-2%;bottom:-9%;height:14%;background:#000;filter:blur(90px);opacity:.75;transform:translateZ(-60px) rotateX(70deg)"></div>
    ${edges}
    <div style="position:absolute;inset:-6px;border-radius:40px;background:linear-gradient(135deg,${t.b}cc,${t.a}22 35%,#ffffff10 60%,${t.a}99);transform:translateZ(-1px)"></div>
    <div style="position:absolute;inset:0;border-radius:34px;overflow:hidden;box-shadow:0 120px 220px #000c,0 0 160px ${t.a}44"><iframe src="${src(id, 'a')}" width="${W}" height="${H}"></iframe>
      <div style="position:absolute;inset:0;background:linear-gradient(112deg,#ffffff1f 0%,#ffffff05 28%,transparent 45%)"></div></div>
  </div></div>${TEXT(t)}`);
}

// every panel lifted to its own height
function exploded(id, t) {
  const k = 2.0, W = 900 * k, H = 600 * k;
  const lift = { top: 330, 'p-sample': 260, 'p-root': 330, 'p-env': 170, 'p-voice': 210, 'p-fx': 120, 'p-keys': 60 };
  const layer = (name, zz) => {
    const [x, y, w, h] = rect(name);
    return `<div class="p3" style="left:${x * k}px;top:${y * k}px;width:${w * k}px;height:${h * k}px;transform:translateZ(${zz}px)">
      <div style="position:absolute;inset:0;border-radius:${15 * k}px;overflow:hidden;box-shadow:0 ${zz / 3}px ${zz}px #000a,0 0 ${zz / 2}px ${t.a}30;border:2px solid ${t.a}44">
      <div style="position:absolute;inset:0;${bg(id, 'a', W, H, x * k, y * k)}"></div></div></div>`;
  };
  const stalks = Object.keys(lift).filter(n => n !== 'top').map(n => { const [x, y, w, h] = rect(n); return `<div class="p3" style="left:${(x + w / 2) * k}px;top:${(y + h / 2) * k}px;width:3px;height:${lift[n]}px;background:linear-gradient(${t.a}aa,transparent);transform-origin:top;transform:rotateX(-90deg)"></div>`; }).join('');
  return BASE(t, `<div class="scene" style="perspective:5200px;perspective-origin:70% 40%">
  <div class="p3" style="left:1700px;top:640px;width:${W}px;height:${H}px;transform:rotateX(58deg) rotateZ(-34deg)">
    <div style="position:absolute;inset:0;border-radius:40px;overflow:hidden;opacity:.9;filter:brightness(.8);box-shadow:0 0 200px ${t.a}55;${bg(id, 'a', W, H, 0, 0)}"></div>
    ${stalks}
    ${Object.keys(lift).map(n => layer(n, lift[n])).join('')}
  </div></div>${TEXT(t, true)}`);
}

// the effects panel large, the rest of the plugin soft behind it
function closeup(id, t) {
  const [x, y, w, h] = rect('p-fx'), k = 5.4, W = 900 * k, H = 600 * k;
  const bw = 2400, bh = bw * 600 / 900;
  return BASE(t, `<div class="scene" style="perspective:3000px;perspective-origin:50% 50%">
    <div class="p3" style="left:1150px;top:300px;width:${bw}px;height:${bh}px;transform:translateZ(-700px) rotateY(-14deg);filter:blur(10px) brightness(.55)"><div style="position:absolute;inset:0;${bg(id, 'b', bw, bh, 0, 0)}"></div></div>
    <div class="p3" style="left:1500px;top:${(2160 - h * k) / 2 + 40}px;width:${w * k}px;height:${h * k}px;transform:rotateY(-17deg) rotateX(7deg) rotateZ(.8deg)">
      <div style="position:absolute;inset:0;border-radius:${15 * k}px;overflow:hidden;box-shadow:0 110px 200px #000d,0 0 220px ${t.a}55;border:3px solid ${t.a}66">
        <div style="position:absolute;inset:0;${bg(id, 'b', W, H, x * k, y * k)}"></div>
        <div style="position:absolute;inset:0;background:linear-gradient(112deg,#ffffff1a 0%,transparent 40%)"></div></div></div></div>
    <div class="txt" style="top:180px"><div class="eyebrow">EFFECTS</div><div class="lead" style="font-size:70px;color:#eceef2;max-width:760px;margin-top:30px">Drag to reorder.<br>Scroll to shape.</div>
    <div class="lead" style="font-size:46px">Delay, reverb, saturation and a three-band filter in a chain you arrange yourself.</div></div>
    <div class="tag">${t.name.toUpperCase()} · <b>CHROMATIC SAMPLER</b></div>`);
}

// all colours side by side
function lineup() {
  const ids = Object.keys(T), cw = 1000, ch = cw * 600 / 900, t = T.ocean;
  const cards = ids.map((id, i) => `<div class="p3" style="left:${120 + i * 560}px;top:${700 + i * 14}px;width:${cw}px;height:${ch}px;transform:rotateY(-24deg) rotateX(4deg)">
    <div style="position:absolute;inset:0;border-radius:26px;overflow:hidden;box-shadow:0 70px 120px #000c,0 0 120px ${T[id].a}55;border:2px solid ${T[id].a}66"><iframe src="${src(id, 'a')}" width="${cw}" height="${ch}"></iframe>
    <div style="position:absolute;inset:0;background:linear-gradient(112deg,#ffffff1a,transparent 40%)"></div></div>
    <div style="position:absolute;left:0;top:${ch + 50}px;font-size:56px;font-weight:600;letter-spacing:.25em;color:${T[id].b}">${T[id].name.toUpperCase()}</div></div>`).join('');
  return BASE(t, `<div class="scene" style="perspective:4200px;perspective-origin:50% 45%">${cards}</div>
  <div class="txt" style="top:130px"><div class="eyebrow">SELEVEN AUDIO</div><h1 style="font-size:150px">Chromatic <em>Sampler</em></h1></div>
  <div class="tag">FIVE COLOURS · <b>YOUR PICK</b></div>`);
}

for (const [id, t] of Object.entries(T)) {
  fs.writeFileSync(path.join(__dirname, 'scenes', `hero-${id}.html`), hero(id, t));
  fs.writeFileSync(path.join(__dirname, 'scenes', `exploded-${id}.html`), exploded(id, t));
  fs.writeFileSync(path.join(__dirname, 'scenes', `closeup-${id}.html`), closeup(id, t));
}
fs.writeFileSync(path.join(__dirname, 'scenes', 'lineup.html'), lineup());
console.log('scenes written');
