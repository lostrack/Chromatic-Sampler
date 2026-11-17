// 6 square Instagram posts (2160x2160) in the violet theme. usage: node posts.js ui6 | node posts.js [shoot]
const fs = require('fs'), path = require('path'), os = require('os');
const { execFileSync } = require('child_process');
const dir = __dirname, D = dir.replace(/\\/g, '/');
const CH = 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const prof = path.join(os.tmpdir(), 'chprof-marketing');
const run = (args, input) => execFileSync(CH, ['--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars', '--user-data-dir=' + prof, ...args, 'file:///' + input], { timeout: 120000, stdio: 'ignore' });

if (process.argv[2] === 'ui6') {            // 6x screenshots for sharp big crops
  fs.mkdirSync(path.join(dir, 'ui6'), { recursive: true });
  for (const v of ['a', 'b'])
    run(['--force-device-scale-factor=6', '--window-size=900,600', '--virtual-time-budget=3000', `--screenshot=${D}/ui6/violet-${v}.png`], `${D}/src/ui-violet-${v}.html`);
  process.exit(0);
}

const T = JSON.parse(fs.readFileSync(path.join(dir, 'themes.json'), 'utf8')).violet;
const R = JSON.parse(fs.readFileSync(path.join(dir, 'rects.json'), 'utf8'));
const z = R.app[2] / 900;
const rc = n => R[n].map(v => v / z);
const U = (v) => v === 'b' ? '../ui/violet-b.png' : `../ui6/violet-${v}.png`;

// crop of the 900x600 ui at k px per ui unit; region [x,y,w,h] in ui units
function crop(v, [x, y, w, h], k, extra = '') {
  return `<div class="crop" style="width:${w * k}px;height:${h * k}px;border-radius:${12 * k}px;background:url(${U(v)}) ${-x * k}px ${-y * k}px/${900 * k}px ${600 * k}px no-repeat;${extra}"></div>`;
}
const union = (...n) => { const r = n.map(rc), x = Math.min(...r.map(a => a[0])), y = Math.min(...r.map(a => a[1])); return [x, y, Math.max(...r.map(a => a[0] + a[2])) - x, Math.max(...r.map(a => a[1] + a[3])) - y]; };
const W = 1980;
const fit = reg => W / reg[2];

const card = (big, small) => `<div class="card"><b>${big}</b><span>${small}</span></div>`;
const cards = (...c) => `<div class="cards">${c.map(a => card(...a)).join('')}</div>`;
const chips = (...c) => `<div class="chips">${c.map(a => `<span>${a}</span>`).join('')}</div>`;

const page = (n, eyebrow, h1, sub, body) => `<!doctype html><html><head><meta charset="utf-8"><title>post ${n}</title><style>
*{box-sizing:border-box}
html,body{margin:0;width:2160px;height:2160px;overflow:hidden;background:#07050e;font-family:'Inter Tight','Segoe UI Variable Display','Segoe UI',system-ui,sans-serif;color:#efeaff}
.bg{position:absolute;inset:0;background:
 radial-gradient(ellipse 70% 45% at 85% 8%,${T.glow}66,transparent 70%),
 radial-gradient(ellipse 60% 40% at 5% 100%,${T.glow}55,transparent 70%),
 linear-gradient(180deg,#0d0818,#07050e)}
.grid{position:absolute;inset:0;background-image:linear-gradient(#ffffff08 2px,transparent 2px),linear-gradient(90deg,#ffffff08 2px,transparent 2px);background-size:90px 90px;mask-image:radial-gradient(ellipse 90% 90% at 50% 50%,#000 30%,transparent 100%)}
.wrap{position:absolute;inset:90px;display:flex;flex-direction:column;gap:40px}
.eyebrow{font-size:36px;letter-spacing:.4em;color:${T.b};font-weight:600}
h1{margin:14px 0 0;font-size:138px;line-height:1;font-weight:700;letter-spacing:-.025em}
h1 em{font-style:normal;color:${T.a};text-shadow:0 0 80px ${T.a}88}
.sub{margin-top:16px;font-size:46px;color:#a79fbd;font-weight:500}
.crop{position:relative;flex:none;border:3px solid ${T.a}66;box-shadow:0 50px 120px #000c,0 0 140px ${T.a}40;overflow:hidden}
.crop::after{content:'';position:absolute;inset:0;background:linear-gradient(115deg,#ffffff18,transparent 38%)}
.row{display:flex;gap:40px;align-items:stretch}
.row .crop{flex:none}
.cards{display:flex;gap:30px;flex:1}
.card{flex:1;display:flex;flex-direction:column;justify-content:center;gap:12px;padding:30px 36px;border-radius:32px;border:3px solid ${T.a}55;background:linear-gradient(160deg,${T.a}26,#ffffff08)}
.card b{font-size:76px;line-height:1.05;color:#fff;font-weight:700}
.card span{font-size:44px;line-height:1.25;color:${T.b};font-weight:500}
.stack{display:flex;flex-direction:column;gap:26px;flex:1}
.stack .card{flex:1}
.chips{display:flex;gap:20px;flex:1}
.chips span{flex:1;display:flex;align-items:center;justify-content:center;font-size:44px;padding:16px 20px;border-radius:999px;border:3px solid ${T.a}77;background:${T.a}22;color:${T.b};font-weight:600}
.foot{margin-top:auto;display:flex;justify-content:space-between;align-items:center;font-size:34px;letter-spacing:.32em;color:#7d7595;font-weight:600}
.foot b{color:${T.b};font-weight:600}
</style></head><body><div class="bg"></div><div class="grid"></div><div class="wrap">
<div><div class="eyebrow">${eyebrow}</div><h1>${h1}</h1><div class="sub">${sub}</div></div>
${body}
<div class="foot"><span>SELEVEN AUDIO · <b>CHROMATIC SAMPLER</b></span><span>0${n} / 06</span></div></div></body></html>`;

const posts = [];

// 1 cover: whole window
{ const all = [0, 0, 900, 600], k = W / 900;
  posts.push(page(1, 'SELEVEN AUDIO', 'Chromatic <em>Sampler</em>', 'Drop in any sound. Play it across the keys.',
    crop('a', all, k) + cards(['Auto pitch', 'Finds the root note'], ['4 effects', 'Reverb · Delay · Saturation · Filter'], ['VST3', 'Plus a Standalone app']))); }

// 2 pitch
{ const s = rc('p-sample'), r = rc('p-root'), ks = fit(s), kr = 945 / r[2];
  posts.push(page(2, 'AUTO ROOT', 'It hears <em>the pitch.</em>', 'Detects the note of your sample for you.',
    crop('a', s, ks) + `<div class="row">${crop('a', r, kr)}<div class="stack">${card('Pitch detected', 'Reads the root of any sample')}${card('Snapped to keys', 'Plays in tune from the first note')}${card('Fine tune', 'Cents control and Auto toggle')}</div></div>` + chips('Auto root', 'Fine tune', 'Octave shift', 'Clear'))); }

// 3 envelope + voice + keys
{ const ev = union('p-env', 'p-voice'), k1 = fit(ev), kk = fit(rc('p-keys'));
  posts.push(page(3, 'ENVELOPE & VOICE', 'Shape <em>every note.</em>', 'ADSR with curves, poly or mono, glide.',
    crop('a', ev, k1) + crop('a', rc('p-keys'), kk) + cards(['808 · Pluck', 'Pad · Stab presets'], ['Poly / Mono', 'Portamento and legato'], ['Curved ADSR', 'Drag the envelope points']))); }

// 4 reverb
{ const f = rc('p-fx'), k = fit(f);
  posts.push(page(4, 'EFFECTS · REVERB', 'Reverb <em>you can see.</em>', 'A live tail view with low and high cut.',
    crop('a', f, k) + cards(['3 room sizes', 'Small · Medium · Large'], ['Low & high cut', 'Keep the tail clean'], ['Drag to reorder', 'Four effects, your order']))); }

// 5 filter
{ const f = rc('p-fx'), k = fit(f);
  posts.push(page(5, 'EFFECTS · FILTER', 'Three-band <em>filter.</em>', 'Low cut, mid bell and high cut in one view.',
    crop('b', f, k) + cards(['6 · 12 · 24 dB', 'Scroll the wheel to set the slope'], ['Mid bell', 'Gain, frequency and width'], ['Drag the nodes', 'Hear it as you move']))); }

// 6 keys + closing
{ const kb = rc('p-keys'), s = rc('p-sample');
  posts.push(page(6, 'PLAY IT', 'Chromatic <em>across the keys.</em>', 'VST3 and Standalone. By Seleven Audio.',
    crop('a', s, fit(s)) + crop('a', kb, fit(kb)) + cards(['VST3', 'Plugin for your DAW'], ['Standalone', 'Runs on its own'], ['Seleven Audio', 'Chromatic Sampler']))); }

fs.mkdirSync(path.join(dir, 'posts'), { recursive: true });
fs.mkdirSync(path.join(dir, 'renders'), { recursive: true });
posts.forEach((h, i) => fs.writeFileSync(path.join(dir, 'posts', `post-${i + 1}.html`), h));
console.log('posts written');

if (process.argv[2] === 'shoot')
  posts.forEach((_, i) => { run(['--force-device-scale-factor=1', '--window-size=2160,2160', '--virtual-time-budget=3000', `--screenshot=${D}/renders/violet-post-${i + 1}.png`], `${D}/posts/post-${i + 1}.html`); console.log('post', i + 1); });
