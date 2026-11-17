// Screenshots every in-plugin theme from Resources/ui/index.html with demo state. usage: node themes-shot.js [menu]
// Reads build.js only for the demo state block; writes src/theme-<id>.html and ui/theme-<id>.png. No other importers.
const fs = require('fs'), path = require('path'), os = require('os');
const { execFileSync } = require('child_process');
const dir = __dirname, D = dir.replace(/\\/g, '/');
const CH = 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const prof = path.join(os.tmpdir(), 'chprof-marketing');
const html = fs.readFileSync(path.join(dir, '..', 'Resources', 'ui', 'index.html'), 'utf8');
const bsrc = fs.readFileSync(path.join(dir, 'build.js'), 'utf8');
const state = new Function(bsrc.slice(bsrc.indexOf('// a decaying'), bsrc.indexOf('function themed')) + '; return state;')();
const menu = process.argv[2] === 'menu';
const ids = ['ocean', 'glacier', 'nebula', 'ember', 'mint', 'glass'];
const hook = "if (B) B.addEventListener('state', applyState);";
if (!html.includes(hook)) throw new Error('hook missing');
fs.mkdirSync(path.join(dir, 'ui'), { recursive: true });
for (const id of ids) {
  let h = html.replace("localStorage.getItem(THEME_KEY) || 'ocean'", `'${id}'`).replace(hook, hook + `
window.__demo = (st, sel, chord) => { applyState(st); selFx = sel; fxListKey = ''; syncControls(); chord.forEach(n => press(n, .8)); };`);
  h = h.replace('</body>', `<script>setTimeout(() => { window.__demo(${JSON.stringify(state)}, 1, [60, 64, 67]); ${menu ? "document.getElementById('menu').hidden = false;" : ''} }, 400);</script></body>`);
  const f = path.join(dir, 'src', `theme-${id}.html`);
  fs.writeFileSync(f, h);
  execFileSync(CH, ['--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars', '--user-data-dir=' + prof, '--force-device-scale-factor=2', '--window-size=900,600', '--virtual-time-budget=3000', `--screenshot=${D}/ui/theme-${id}${menu ? '-menu' : ''}.png`, 'file:///' + f.replace(/\\/g, '/')], { timeout: 120000, stdio: 'ignore' });
  console.log('theme', id);
}
