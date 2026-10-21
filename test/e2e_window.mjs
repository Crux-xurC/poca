// E2E for Poca's windows, driven over CDP (pages), wmctrl (counting the
// windows) and XTest (Ctrl+N, which only the window's views see).
//
// A Poca with its own config and runtime directories (never ~/.config/poca)
// starts test/auth-app from box: "auth-test" on :4320 with "auth": true and
// "control" on :4321 without. Then:
//   - window.open with features from auth-test opens a second window, and the
//     page there is OK: a window keeps who opened it, as a tab does; and
//     window.open returns it. Without a user gesture it is null (Chromium's
//     pop-up blocker, as in a browser)
//   - window.open without features is still a tab (no new window)
//   - a window control opens to auth-test is NG
//   - Ctrl+N opens a window with poca://home
//   - closing one window leaves Poca and the programs running; closing the
//     last one stops them and ends Poca
//
// Needs a built build/Release/poca, an X display, /usr/bin/python3,
// /usr/bin/wmctrl and libXtst. Run: node test/e2e_window.mjs

import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const POCA = path.join(ROOT, 'build/Release/poca');
const SERVER = path.join(ROOT, 'test/auth-app/server.py');
const AUTH = 4320, CONTROL = 4321, CDP = 9334;

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'poca-e2e-window-'));
const config = path.join(tmp, 'config'), runtime = path.join(tmp, 'runtime');
const logs = path.join(tmp, 'logs');
for (const d of [path.join(config, 'poca'), runtime, logs]) fs.mkdirSync(d, { recursive: true, mode: 0o700 });

const row = (name, port, auth) => ({
  name, ports: [port], auth,
  run: ['/usr/bin/python3', '-I', SERVER, '--port', String(port)],
  cwd: tmp, log: path.join(logs, name + '.log'),
});
fs.writeFileSync(path.join(config, 'poca/box.json'), JSON.stringify({
  programs: [row('auth-test', AUTH, true), row('control', CONTROL, false)],
}));
fs.writeFileSync(path.join(config, 'poca/bookmarks.json'), '[]');

let failures = 0;
const check = (ok, what, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${ok || !detail ? '' : ' -- ' + detail}`);
  if (!ok) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(what, fn, ms = 10000) {
  const end = Date.now() + ms;
  for (;;) {
    try { const v = await fn(); if (v) return v; } catch {}
    if (Date.now() > end) throw new Error('timed out: ' + what);
    await sleep(100);
  }
}
const portOpen = async (port) => {
  try { await fetch(`http://127.0.0.1:${port}/whoami`); return true; } catch { return false; }
};

for (const port of [AUTH, CONTROL, CDP]) {
  if (await portOpen(port)) throw new Error(`port ${port} is in use`);
}

// --- the windows (X) -------------------------------------------------------

// Poca's top-level windows (WM_CLASS poca.poca) of this run's pid.
const windows = () => execFileSync('/usr/bin/wmctrl', ['-lpx']).toString().split('\n')
  .map((l) => l.trim().split(/\s+/))
  .filter((f) => f[3] === 'poca.poca' && Number(f[2]) === poca.pid)
  .map((f) => f[0]);
const windowCount = (n, what) => until(what, () => windows().length === n, 8000).then(() => true, () => false);

// Ctrl+N into the focused window, through XTest.
const XTEST = `
import ctypes, time
x = ctypes.CDLL('libX11.so.6'); t = ctypes.CDLL('libXtst.so.6')
x.XOpenDisplay.restype = ctypes.c_void_p
d = ctypes.c_void_p(x.XOpenDisplay(None))
x.XStringToKeysym.restype = ctypes.c_ulong
code = lambda s: x.XKeysymToKeycode(d, ctypes.c_ulong(x.XStringToKeysym(s.encode())))
ctrl, n = code('Control_L'), code('n')
for k, down in ((ctrl, 1), (n, 1), (n, 0), (ctrl, 0)):
    t.XTestFakeKeyEvent(d, k, down, 0); x.XFlush(d); time.sleep(0.05)
`;
const ctrlN = (win) => {
  execFileSync('/usr/bin/wmctrl', ['-i', '-a', win]);
  execFileSync('/usr/bin/sleep', ['0.5']);
  execFileSync('/usr/bin/python3', ['-I', '-c', XTEST]);
};

// --- CDP -------------------------------------------------------------------

class Page {
  static async open(target) {
    const p = new Page(new WebSocket(target.webSocketDebuggerUrl));
    await new Promise((res, rej) => { p.ws.onopen = res; p.ws.onerror = rej; });
    return p;
  }
  constructor(ws) {
    this.ws = ws; this.id = 0; this.wait = new Map();
    ws.onmessage = (m) => {
      const msg = JSON.parse(m.data);
      const w = this.wait.get(msg.id);
      if (w) { this.wait.delete(msg.id); msg.error ? w.rej(new Error(msg.error.message)) : w.res(msg.result); }
    };
  }
  send(method, params = {}) {
    const id = ++this.id;
    this.ws.send(JSON.stringify({ id, method, params }));
    return new Promise((res, rej) => this.wait.set(id, { res, rej }));
  }
  async eval(expression, userGesture = false) {
    const r = await this.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true, userGesture });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || 'evaluate failed');
    return r.result.value;
  }
  close() { this.ws.close(); }
}
const targets = async () => (await fetch(`http://127.0.0.1:${CDP}/json/list`)).json();
const pageAt = (prefix) => until('a tab at ' + prefix, async () =>
  (await targets()).find((t) => t.type === 'page' && t.url.startsWith(prefix)));
const resultText = (page) => until('the page result', () =>
  page.eval(`document.getElementById('result')?.textContent || ''`));

// A page that has had no user gesture yet: control's own, in a new tab of box.
async function nogestureFresh() {
  await box.eval(`!!window.open('http://127.0.0.1:${AUTH}/?fresh', '_blank')`, true);
  const fresh = await Page.open(await pageAt(`http://127.0.0.1:${AUTH}/?fresh`));
  await resultText(fresh);
  const got = await fresh.eval(`window.open('/?nogesture', 'w9', 'width=800,height=600') === null`, false);
  fresh.close();
  return got;
}

// --- run -------------------------------------------------------------------

const poca = spawn(POCA, [`--remote-debugging-port=${CDP}`, '--url=poca://box/'], {
  env: { ...process.env, XDG_CONFIG_HOME: config, XDG_RUNTIME_DIR: runtime },
  stdio: 'ignore',
});
const pocaExit = new Promise((res) => poca.on('exit', res));

let box = null;
try {
  box = await Page.open(await pageAt('poca://box'));
  for (const name of ['auth-test', 'control']) {
    const answer = await box.eval(`fetch('start?name=${name}', {method: 'POST'}).then((r) => r.json())`);
    check(answer.error === '', `box starts ${name}`, JSON.stringify(answer));
  }
  await until('both programs listening', async () => (await portOpen(AUTH)) && (await portOpen(CONTROL)));
  check(await windowCount(1, 'one window'), 'Poca starts with one window', String(windows().length));
  const first = windows()[0];

  await box.eval(`!!window.open('http://127.0.0.1:${AUTH}/', '_blank')`, true);
  const authPage = await Page.open(await pageAt(`http://127.0.0.1:${AUTH}/`));
  check(await resultText(authPage) === 'OK', 'box opens auth-test in a tab: OK');
  check(await windowCount(1, 'still one window'), 'window.open without features: a tab, no new window',
        String(windows().length));

  check(await authPage.eval(`!!window.open('/?win', 'w1', 'width=800,height=600')`, true), 'window.open returns the window (not null)');
  const win = await Page.open(await pageAt(`http://127.0.0.1:${AUTH}/?win`));
  check(await windowCount(2, 'two windows'), 'window.open with features: a second window', String(windows().length));
  check(await resultText(win) === 'OK', 'the page auth-test opened in a window: OK (inherited)');
  const r = await win.eval(`fetch('/whoami').then((r) => r.json())`);
  check(r.result === 'OK', "the new window's own fetch: OK", JSON.stringify(r));

  // Chromium's pop-up blocker, before Poca: a page with no key press or click for a while gets null
  check(await nogestureFresh(), 'window.open without a user gesture: refused (null), as in a browser');
  await authPage.eval(`!!window.open('/?w2', 'w2', 'width=800,height=600')`, true);
  await pageAt(`http://127.0.0.1:${AUTH}/?w2`);
  check(await windowCount(3, 'three windows'), 'a second window from the same page', String(windows().length));

  await win.eval(`!!window.open('/?tab-in-win', '_blank')`, true);
  const tabInWin = await Page.open(await pageAt(`http://127.0.0.1:${AUTH}/?tab-in-win`));
  check(await resultText(tabInWin) === 'OK', 'a tab opened from the second window: OK');
  check(await windowCount(3, 'still three windows'), '... and it is a tab, not a window', String(windows().length));

  await box.send('Page.navigate', { url: `http://127.0.0.1:${CONTROL}/` });
  await until('box shows control', async () => (await box.eval('location.href')).includes(`:${CONTROL}/`));
  await box.eval(`!!window.open('http://127.0.0.1:${AUTH}/?from-control', 'w3', 'width=800,height=600')`, true);
  const fromControl = await Page.open(await pageAt(`http://127.0.0.1:${AUTH}/?from-control`));
  check(await windowCount(4, 'four windows'), 'control opens a window', String(windows().length));
  check(await resultText(fromControl) === 'NG', 'a window control opens to auth-test: NG');

  ctrlN(first);
  const home = await until('a poca://home tab', async () =>
    (await targets()).find((t) => t.type === 'page' && t.url.startsWith('poca://home')));
  check(!!home, 'Ctrl+N opens poca://home');
  check(await windowCount(5, 'five windows'), '... in a new window', String(windows().length));

  // Close every window but the first, one by one, through the window manager.
  for (const w of windows().filter((w) => w !== first)) execFileSync('/usr/bin/wmctrl', ['-i', '-c', w]);
  check(await windowCount(1, 'back to one window'), 'the other windows close', String(windows().length));
  await sleep(500);
  check(poca.exitCode === null, 'Poca keeps running while a window is left');
  check((await portOpen(AUTH)) && (await portOpen(CONTROL)), '... and so do auth-test and control');

  execFileSync('/usr/bin/wmctrl', ['-i', '-c', first]);
} catch (e) {
  check(false, 'the run', e.stack || String(e));
  try { console.log('tabs:', (await targets()).map((t) => t.url).join(' ')); } catch {}
  try { for (const w of windows()) execFileSync('/usr/bin/wmctrl', ['-i', '-c', w]); } catch {}
}

const code = await Promise.race([pocaExit, sleep(20000).then(() => 'timeout')]);
check(code !== 'timeout', 'Poca exits when its last window closes');
if (code === 'timeout') poca.kill('SIGKILL');
check(!(await portOpen(AUTH)) && !(await portOpen(CONTROL)), 'closing the last window stops auth-test and control');

fs.rmSync(tmp, { recursive: true, force: true });
console.log(failures ? `${failures} failed` : 'all ok');
process.exit(failures ? 1 : 0);
