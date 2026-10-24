// E2E for the split view and the tab strip's keys. The keys only the window's
// views see go in with xdotool; what the window shows is read from a
// screenshot of the display: each tab is a page of one colour, so the colour
// at the left and at the right of the content says which tabs are shown.
//
// A Poca with its own config and runtime directories (never ~/.config/poca)
// opens T0 (red), whose click opens T1 (green), whose click opens T2 (blue);
// T2 is active. Then:
//   - T2 alone, all across
//   - Ctrl+Shift+S, Enter: T2 and the tab picked (T0) side by side, T2 left;
//     T0 moved next to T2 in the strip, so Ctrl+Tab goes to it and the split
//     stays
//   - Ctrl+Shift+X on T0 (the right side), Enter: T1 comes in its place
//   - Ctrl+Shift+X, Escape: nothing changes
//   - Ctrl+Shift+X, Right, Enter (the other side): the two swap sides
//   - Ctrl+Shift+S in the split: undone, the active tab alone, all across
//   - Ctrl+Shift+PageUp moves the active tab left: Ctrl+Tab goes on from there
//
// Needs a built Poca (POCA, default build/Release/poca), an X display without
// a window manager (DISPLAY; a virtual one -- Xvfb -- keeps the keys and the
// window off the desktop), xdotool (XDOTOOL, default on PATH), ImageMagick
// (magick, import) and /usr/bin/python3. Run: DISPLAY=:99 node test/e2e_split.mjs
import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const POCA = process.env.POCA || path.join(ROOT, 'build/Release/poca');
const XDOTOOL = process.env.XDOTOOL || 'xdotool';
const PAGES = 4330;
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'poca-e2e-split-'));
const config = path.join(tmp, 'config'), runtime = path.join(tmp, 'runtime'), www = path.join(tmp, 'www');
for (const d of [path.join(config, 'poca'), runtime, www]) fs.mkdirSync(d, { recursive: true, mode: 0o700 });
fs.writeFileSync(path.join(config, 'poca/box.json'), JSON.stringify({ programs: [] }));
fs.writeFileSync(path.join(config, 'poca/bookmarks.json'), '[]');
// T0 red -> T1 green -> T2 blue: a click on each opens the next in a tab
const COLORS = { T0: [255, 0, 0], T1: [0, 255, 0], T2: [0, 0, 255] };
const page = (color, next) => `<!doctype html><title>${color}</title>
<body style="margin:0;height:100vh;background:${color}" onclick="${next ? `window.open('/${next}.html', '_blank')` : ''}"></body>`;
fs.writeFileSync(path.join(www, 'T0.html'), page('rgb(255,0,0)', 'T1'));
fs.writeFileSync(path.join(www, 'T1.html'), page('rgb(0,255,0)', 'T2'));
fs.writeFileSync(path.join(www, 'T2.html'), page('rgb(0,0,255)', null));

let failures = 0;
const check = (ok, what, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${ok || !detail ? '' : ' -- ' + detail}`);
  if (!ok) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(what, fn, ms = 15000) {
  const end = Date.now() + ms;
  for (;;) {
    try { const v = await fn(); if (v) return v; } catch {}
    if (Date.now() > end) throw new Error('timed out: ' + what);
    await sleep(200);
  }
}
const x = (...a) => execFileSync(XDOTOOL, a).toString().trim();
const win = () => x('search', '--onlyvisible', '--pid', String(poca.pid), '--class', 'poca').split('\n')[0];
const geometry = () => {
  const g = Object.fromEntries(x('getwindowgeometry', '--shell', win()).split('\n').map((l) => l.split('=')));
  return { x: +g.X, y: +g.Y, w: +g.WIDTH, h: +g.HEIGHT };
};
// with no window manager the keyboard goes to the window under the pointer
const keys = async (...ks) => {
  const g = geometry();
  x('mousemove', '--sync', String(Math.round(g.x + g.w / 2)), String(Math.round(g.y + g.h * 0.7)));
  for (const k of ks) { x('key', '--clearmodifiers', k); await sleep(300); }
  await sleep(700);
};
// the tab whose colour is at (fx, fy) of the window (fractions), or '?'
function tabAt(fx, fy) {
  const g = geometry();
  const shot = path.join(tmp, 'shot.png');
  execFileSync('/usr/bin/import', ['-window', 'root', shot]);
  const px = execFileSync('/usr/bin/magick', [shot, '-format', `%[fx:int(255*p{${Math.round(g.x + g.w * fx)},${Math.round(g.y + g.h * fy)}}.r)],%[fx:int(255*p{${Math.round(g.x + g.w * fx)},${Math.round(g.y + g.h * fy)}}.g)],%[fx:int(255*p{${Math.round(g.x + g.w * fx)},${Math.round(g.y + g.h * fy)}}.b)]`, 'info:']).toString().split(',').map(Number);
  const hit = Object.entries(COLORS).find(([, c]) => c.every((v, i) => Math.abs(v - px[i]) < 40));
  return hit ? hit[0] : '?(' + px.join(',') + ')';
}
// what is shown at the left and at the right of the content: "T2 T2" (one tab) or "T2 T0" (a split)
const shown = () => tabAt(0.2, 0.7) + ' ' + tabAt(0.8, 0.7);
async function expect(want, what) {
  let got = '';
  try { await until(what, () => (got = shown()) === want, 5000); } catch {}
  check(got === want, what, 'shown: ' + got + ', wanted: ' + want);
}
const click = async (fx, fy) => {
  const g = geometry();
  x('mousemove', '--sync', String(Math.round(g.x + g.w * fx)), String(Math.round(g.y + g.h * fy)));
  x('click', '1');
  await sleep(1200);
};

const server = spawn('/usr/bin/python3', ['-I', '-m', 'http.server', '--bind', '127.0.0.1', '--directory', www, String(PAGES)], { stdio: 'ignore' });
let poca = null;
try {
  await until('the pages', async () => { try { return (await fetch(`http://127.0.0.1:${PAGES}/T0.html`)).ok; } catch { return false; } });
  // --no-first-run: no first-run dialog of Chromium (terms of service) in a fresh profile
  poca = spawn(POCA, ['--no-first-run', `--url=http://127.0.0.1:${PAGES}/T0.html`], {
    env: { ...process.env, XDG_CONFIG_HOME: config, XDG_RUNTIME_DIR: runtime },
    stdio: 'ignore',
  });
  await until('Poca\'s window', () => { try { return !!win(); } catch { return false; } });
  await until('T0 drawn', () => shown() === 'T0 T0', 20000);
  await click(0.5, 0.7);
  await until('T1 drawn', () => shown() === 'T1 T1');
  await click(0.5, 0.7);
  await expect('T2 T2', 'three tabs: the last one (T2) alone, all across');

  await keys('ctrl+shift+s', 'Return');
  await expect('T2 T0', 'Ctrl+Shift+S, Enter: T2 and the tab picked (T0) side by side, T2 on the left');

  await keys('ctrl+Tab');
  await expect('T2 T0', 'Ctrl+Tab goes to the other side (T0 moved next to T2): the split stays');

  // T0 (the right side) is active; the strip is T1 T2 T0: the first one picked is T1
  await keys('ctrl+shift+x', 'Return');
  await expect('T2 T1', 'Ctrl+Shift+X, Enter: T1 comes in T0\'s place');

  await keys('ctrl+shift+x', 'Escape');
  await expect('T2 T1', 'Ctrl+Shift+X, Escape: nothing changes');

  // T1 is active; the strip is T0 T2 T1 (T0 went back where T1 was): T0 first, Right is T2 (the other side)
  await keys('ctrl+shift+x', 'Right', 'Return');
  await expect('T1 T2', 'Ctrl+Shift+X, Right (the other side), Enter: the two swap sides');

  await keys('ctrl+shift+s');
  await expect('T1 T1', 'Ctrl+Shift+S in the split: undone, the active tab (T1) alone, all across');

  // the strip is T0 T1 T2; T1 moved left (T1 T0 T2), Ctrl+Tab goes on to T0
  await keys('ctrl+shift+Prior', 'ctrl+Tab');
  await expect('T0 T0', 'Ctrl+Shift+PageUp moves the active tab left: Ctrl+Tab goes on to the one after it (T0)');
} catch (e) {
  check(false, 'the run', e.message);
} finally {
  if (poca) poca.kill('SIGTERM');
  server.kill('SIGTERM');
  await sleep(1500);
  fs.rmSync(tmp, { recursive: true, force: true });
}

console.log(failures ? `${failures} failed` : 'all ok');
process.exit(failures ? 1 : 0);
