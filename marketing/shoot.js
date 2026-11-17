// Screenshots every page with headless Chrome. usage: node shoot.js ui | node shoot.js scenes
const fs = require('fs'), path = require('path'), os = require('os');
const { execFileSync } = require('child_process');
const CH = 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const dir = __dirname.replace(/\\/g, '/');
const prof = path.join(os.tmpdir(), 'chprof-marketing');

function run(args, input) {
  return execFileSync(CH, ['--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars',
    '--user-data-dir=' + prof, ...args, 'file:///' + input], { timeout: 120000, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
}

const mode = process.argv[2];
if (mode === 'ui') {
  for (const f of fs.readdirSync(path.join(__dirname, 'src')).filter(n => n.startsWith('ui-') && n.endsWith('.html'))) {
    const name = f.replace(/^ui-|\.html$/g, '');
    run(['--force-device-scale-factor=4', '--window-size=900,600', '--virtual-time-budget=3000', `--screenshot=${dir}/ui/${name}.png`], `${dir}/src/${f}`);
    console.log('ui', name);
  }
  const dom = run(['--window-size=900,600', '--virtual-time-budget=3000', '--dump-dom'], `${dir}/src/ui-ocean-a.html`);
  const m = dom.match(/<title>([^<]*)<\/title>/);
  fs.writeFileSync(path.join(__dirname, 'rects.json'), m[1].replace(/&quot;/g, '"'));
  console.log('rects', m[1].slice(0, 120));
} else if (mode === 'scenes') {
  for (const f of fs.readdirSync(path.join(__dirname, 'scenes')).filter(n => n.endsWith('.html'))) {
    run(['--force-device-scale-factor=1', '--window-size=3840,2160', '--virtual-time-budget=4000', `--screenshot=${dir}/renders/${f.replace('.html', '.png')}`], `${dir}/scenes/${f}`);
    console.log('scene', f);
  }
}
