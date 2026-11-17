// Builds themed copies of the plugin page with demo state, for screenshots.
// usage: node build.js   -> marketing/src/ui-<theme>-<a|b>.html
const fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'Resources/ui/index.html'), 'utf8');

const THEMES = {
  ocean:  { name: 'Ocean',  a: '#2997ff', b: '#7cc0ff', rgb: '41,151,255',  glow: '#1d6fd1' },
  ember:  { name: 'Ember',  a: '#ff7a2f', b: '#ffb27a', rgb: '255,122,47',  glow: '#d9531a' },
  violet: { name: 'Violet', a: '#a066ff', b: '#c9a8ff', rgb: '160,102,255', glow: '#7a3fe0' },
  mint:   { name: 'Mint',   a: '#1fd6a2', b: '#7cf0cd', rgb: '31,214,162',  glow: '#10a87c' },
  rose:   { name: 'Rose',   a: '#ff4d8d', b: '#ff9cc0', rgb: '255,77,141',  glow: '#d12a66' }
};

// a decaying low pluck: 480 bins of [min, max]
const peaks = [];
for (let i = 0; i < 480; i++) {
  const t = i / 480;
  const env = Math.exp(-3.1 * t) * (0.25 + 0.75 * Math.min(1, t * 40));
  const body = 0.78 + 0.22 * Math.sin(i * 0.9) * Math.cos(i * 0.23);
  const a = Math.min(0.96, env * body * 0.95 + (t < 0.03 ? 0.15 : 0));
  peaks.push(-a * (0.9 + 0.1 * Math.sin(i * 1.7)), a);
}

const state = {
  adsrOn: 1, attack: 0.004, decay: 0.62, sustain: 0.38, release: 0.52, curveA: 0, curveD: 0.25, curveR: 0.3,
  mono: 0, legato: 0, glideOn: 0, glide: 0.09, fine: 0, rootShift: 0, volume: 0.8, autoTrim: 1,
  revOn: 1, revRoom: 0.62, revAmt: 0.42, revLo: 120, revHi: 9000,
  dlyOn: 1, dlySync: 1, dlyDiv: 3, dlyMs: 250, dlyPing: 1, dlyFb: 0.42, dlyAmt: 0.35, dlySpread: 0.9,
  satOn: 1, satDrive: 0.45, satMatch: 1, satMode: 1, fxOrder: 12,
  fltOn: 1, fltHp: 90, fltLp: 14000, fltHpS: 1, fltLpS: 2, fltMF: 2400, fltMG: 3.5, fltMQ: 1.1,
  sampleStart: 0, sampleEnd: 1, fadeIn: 0, fadeOut: 0, foldRoot: 1, stdNames: 0, autoTune: 1, hostBpm: 140,
  sample: { loaded: true, seq: 1, name: 'Velvet Pluck.wav', duration: 1.84, hasPitch: true, midi: 55, confidence: 0.94,
            removedMs: 12, pitchHz: 196, channels: 2, peaks, viewStart: 0, viewEnd: 1 },
  snap: { stage: 2, frac: 0.4, level: 0.38, playPos: 0.23 }
};

function themed(t, variant) {
  let h = html.split('#2997ff').join(t.a).split('#7cc0ff').join(t.b).split('41,151,255').join(t.rgb);
  const hook = "if (B) B.addEventListener('state', applyState);";
  if (!h.includes(hook)) throw new Error('hook line not found');
  h = h.replace(hook, hook + `
window.__demo = (st, sel, chord) => { applyState(st); selFx = sel; fxListKey = ''; syncControls(); chord.forEach(n => press(n, .8)); };`);
  const sel = variant === 'a' ? 1 : 3;
  const chord = variant === 'a' ? [60, 64, 67] : [62, 65, 69];
  h = h.replace('</body>', `<script>setTimeout(() => {
  window.__demo(${JSON.stringify(state)}, ${sel}, ${JSON.stringify(chord)});
  const r = document.querySelector('#app').getBoundingClientRect();
  const out = {}; document.querySelectorAll('.panel,header.top').forEach(e => { const b = e.getBoundingClientRect(); out[e.className.split(' ').pop()] = [b.x - r.x, b.y - r.y, b.width, b.height]; });
  out.app = [r.x, r.y, r.width, r.height]; out.iw = [innerWidth, innerHeight];
  document.title = JSON.stringify(out);
}, 400);</script></body>`);
  return h;
}

for (const [id, t] of Object.entries(THEMES))
  for (const v of ['a', 'b'])
    fs.writeFileSync(path.join(__dirname, 'src', `ui-${id}-${v}.html`), themed(t, v));
fs.writeFileSync(path.join(__dirname, 'themes.json'), JSON.stringify(THEMES, null, 1));
console.log('built', Object.keys(THEMES).length * 2, 'pages');
