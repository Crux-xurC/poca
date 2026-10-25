// E2E for Poca's authentication, driven over CDP.
//
// A Poca with its own config and runtime directories (never ~/.config/poca)
// starts two copies of test/auth-app from box: "auth-test" on :4310 with
// "auth": true and "control" on :4311 without. Then:
//   - the bookmark opens auth-test and it says OK; its own fetches are OK
//   - control gets neither the header nor POCA_SECRET (Poca is started with a
//     POCA_SECRET of its own, which must not leak through)
//   - from auth-test, requests to control and redirects to control carry no
//     header; from control, fetches and navigations to auth-test are NG, and a
//     forged header is dropped
//   - an ordinary browser (headless Chromium) and a plain fetch see NG
//   - the secret is in no log and in no file Poca leaves; closing Poca stops
//     both programs
//
//   - WebSockets go through a one-shot ticket from /ticket: auth-test's page
//     opens one; a used, expired, made-up or missing ticket is refused, and
//     control and anything outside Poca cannot get a ticket
//   - a navigation within auth-test is OK; a tab control opens (window.open)
//     and a redirect by control, even of a page the person opened, are NG
//
// Not covered (the window's own views): the url field and Ctrl+R.
//
// Needs a built Poca (POCA, default build/Release/poca), a display (the window shows briefly),
// /usr/bin/python3 and /usr/bin/chromium. Run: node test/e2e_auth.mjs

import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const POCA = process.env.POCA || path.join(ROOT, 'build/Release/poca');
const SERVER = path.join(ROOT, 'test/auth-app/server.py');
const AUTH = 4310, CONTROL = 4311, CDP = 9333, TTL = 2;

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'poca-e2e-auth-'));
const config = path.join(tmp, 'config'), runtime = path.join(tmp, 'runtime');
const logs = path.join(tmp, 'logs');
for (const d of [path.join(config, 'poca'), runtime, logs]) fs.mkdirSync(d, { recursive: true, mode: 0o700 });

const row = (name, port, auth) => ({
  name, ports: [port], auth,
  run: ['/usr/bin/python3', '-I', SERVER, '--port', String(port), '--ticket-ttl', String(TTL)],
  cwd: tmp, log: path.join(logs, name + '.log'),
});
fs.writeFileSync(path.join(config, 'poca/box.json'), JSON.stringify({
  programs: [row('auth-test', AUTH, true), row('control', CONTROL, false)],
}));
fs.writeFileSync(path.join(config, 'poca/bookmarks.json'), JSON.stringify([
  { name: 'auth-test', url: `http://127.0.0.1:${AUTH}/` },
]));

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
const wsText = (page) => until("the page's WebSocket", async () => {
  const t = await page.eval(`document.getElementById('ws')?.textContent || ''`);
  return t.startsWith('WS ') && !t.endsWith('…') && t;
});
// From inside a page: 'open' when the WebSocket says hello, else 'refused'.
const tryWs = (page, url) => page.eval(`new Promise((res) => {
  const ws = new WebSocket(${JSON.stringify(url)});
  ws.onmessage = (e) => { res(e.data === 'hello' ? 'open' : 'odd'); ws.close(); };
  ws.onerror = () => res('refused');
})`);
const ticketFrom = (page, url) => page.eval(
  `fetch(${JSON.stringify(url)}, {cache: 'no-store'}).then(async (r) => ({status: r.status, ...(await r.json())}))`);

// --- run -------------------------------------------------------------------

const FAKE = 'inherited-by-poca-must-not-leak';
// --no-first-run: no first-run dialog of Chromium (terms of service) in a fresh profile
const poca = spawn(POCA, [`--remote-debugging-port=${CDP}`, '--url=poca://box/', '--no-first-run'], {
  env: { ...process.env, XDG_CONFIG_HOME: config, XDG_RUNTIME_DIR: runtime, POCA_SECRET: FAKE },
  stdio: 'ignore',
});
const pocaExit = new Promise((res) => poca.on('exit', res));

let secret = '';
let box = null;
try {
  box = await Page.open(await pageAt('poca://box'));
  for (const name of ['auth-test', 'control']) {
    const answer = await box.eval(`fetch('start?name=${name}', {method: 'POST'}).then((r) => r.json())`);
    check(answer.error === '', `box starts ${name}`, JSON.stringify(answer));
  }
  await until('both programs listening', async () => (await portOpen(AUTH)) && (await portOpen(CONTROL)));

  // The secret as auth-test received it, to look for it elsewhere later.
  const pid = execFileSync('ss', ['-Hltnp', `sport = :${AUTH}`]).toString().match(/pid=(\d+)/)?.[1];
  const environ = (p) => fs.readFileSync(`/proc/${p}/environ`).toString().split('\0');
  secret = environ(pid).find((kv) => kv.startsWith('POCA_SECRET='))?.slice(12) || '';
  check(/^[0-9a-f]{64}$/.test(secret), 'auth-test got a 64-hex POCA_SECRET', secret ? 'malformed' : 'missing');
  check(secret !== FAKE, "auth-test's secret is Poca's own, not the inherited one");
  const cpid = execFileSync('ss', ['-Hltnp', `sport = :${CONTROL}`]).toString().match(/pid=(\d+)/)?.[1];
  check(!environ(cpid).some((kv) => kv.startsWith('POCA_SECRET=')), 'control has no POCA_SECRET');

  // The bookmark (target=_blank: a new tab).
  await box.eval(`(async () => {
    for (let i = 0; i < 50 && !document.querySelector('#bookmarks a'); i++) await new Promise((r) => setTimeout(r, 100));
    document.querySelector('#bookmarks a').click();
  })()`, true);
  const authPage = await Page.open(await pageAt(`http://127.0.0.1:${AUTH}/`));
  check(await resultText(authPage) === 'OK', 'the bookmark opens auth-test: OK');
  check(await wsText(authPage) === 'WS OK', "auth-test's page opens its WebSocket with a ticket: WS OK");

  const WS = `ws://127.0.0.1:${AUTH}/ws`;
  let t = await ticketFrom(authPage, '/ticket');
  check(t.status === 200 && t.ticket, 'auth-test gets a ticket', JSON.stringify(t));
  check(await tryWs(authPage, `${WS}?ticket=${t.ticket}`) === 'open', 'the ticket opens a WebSocket');
  check(await tryWs(authPage, `${WS}?ticket=${t.ticket}`) === 'refused', 'the same ticket again: refused');
  t = await ticketFrom(authPage, '/ticket');
  await sleep(TTL * 1000 + 500);
  check(await tryWs(authPage, `${WS}?ticket=${t.ticket}`) === 'refused', 'an expired ticket: refused');
  check(await tryWs(authPage, WS) === 'refused', 'a WebSocket without a ticket: refused');
  check(await tryWs(authPage, `${WS}?ticket=forged`) === 'refused', 'a made-up ticket: refused');

  const whoami = (page, url, init = '{}') =>
    page.eval(`fetch(${JSON.stringify(url)}, ${init}).then((r) => r.json())`);
  let r = await whoami(authPage, '/whoami');
  check(r.result === 'OK', 'auth-test fetching itself: OK', JSON.stringify(r));
  r = await whoami(authPage, `http://127.0.0.1:${CONTROL}/whoami`);
  check(!r.header, 'auth-test fetching control: no header', JSON.stringify(r));
  r = await whoami(authPage, `/redirect?to=${CONTROL}`);
  check(!r.header, 'a redirect from auth-test to control: no header', JSON.stringify(r));
  r = await whoami(authPage, `/redirect?to=${AUTH}`);
  check(r.result === 'OK', 'a redirect from auth-test to itself: OK', JSON.stringify(r));
  r = await whoami(authPage, `http://127.0.0.1:${CONTROL}/redirect?to=${AUTH}`);
  check(r.result === 'NG', 'auth-test fetching control, which redirects back: NG', JSON.stringify(r));
  await authPage.eval(`location.href = '/?again'`);
  await until('auth-test moved within itself', async () => (await authPage.eval('location.href')).endsWith('?again'));
  check(await resultText(authPage) === 'OK', 'a navigation within auth-test: OK');

  // Opened by the person, but redirected by control: from control.
  await box.send('Page.navigate', { url: `http://127.0.0.1:${CONTROL}/redirect?to=${AUTH}` });
  await until('box followed the redirect', async () => (await box.eval('location.href')).includes(`:${AUTH}/whoami`));
  r = JSON.parse(await until('the redirected body', () => box.eval('document.body.innerText')));
  check(r.result === 'NG' && !r.header, 'the person opens control, which redirects to auth-test: NG', JSON.stringify(r));

  // Control, opened by the person (browser-initiated, like the url field).
  await box.send('Page.navigate', { url: `http://127.0.0.1:${CONTROL}/` });
  await until('box shows control', async () => (await box.eval('location.href')).includes(`:${CONTROL}/`));
  const control = box;
  check(await resultText(control) === 'NG', 'control opened by the person: NG (it has no secret)');
  check(await wsText(control) === 'WS NG', "control's own page: WS NG (no ticket without the secret)");
  t = await ticketFrom(control, `http://127.0.0.1:${AUTH}/ticket`);
  check(t.status === 401 && !t.ticket, "control asking auth-test for a ticket: 401", JSON.stringify(t));
  check(await tryWs(control, `ws://127.0.0.1:${AUTH}/ws`) === 'refused', "control opening auth-test's WebSocket: refused");
  r = await whoami(control, '/whoami');
  check(!r.header, 'control fetching itself: no header', JSON.stringify(r));
  r = await whoami(control, `http://127.0.0.1:${AUTH}/whoami`);
  check(r.result === 'NG' && !r.header, 'control fetching auth-test: NG, no header', JSON.stringify(r));
  r = await whoami(control, `http://127.0.0.1:${AUTH}/whoami`, `{headers: {'X-Poca-Secret': 'forged'}}`);
  check(r.result === 'NG' && !r.header, 'a forged header from control is dropped', JSON.stringify(r));
  await control.eval(`location.href = 'http://127.0.0.1:${AUTH}/'`);
  await until('control navigated to auth-test', async () => (await control.eval('location.href')).includes(`:${AUTH}/`));
  check(await resultText(control) === 'NG', 'control navigating to auth-test: NG');
  await control.send('Page.navigate', { url: `http://127.0.0.1:${CONTROL}/` });
  await until('box shows control again', async () => (await control.eval('location.href')).includes(`:${CONTROL}/`));
  await control.eval(`!!window.open('http://127.0.0.1:${AUTH}/?popup')`, true);
  const popup = await Page.open(await pageAt(`http://127.0.0.1:${AUTH}/?popup`));
  check(await resultText(popup) === 'NG', 'a tab control opens with window.open to auth-test: NG');
  popup.close();

  // An ordinary browser and a plain request.
  const profile = fs.mkdtempSync(path.join(tmp, 'chromium-'));
  const dom = execFileSync('/usr/bin/chromium', ['--headless=new', '--disable-gpu', `--user-data-dir=${profile}`,
    '--virtual-time-budget=5000', '--dump-dom', `http://127.0.0.1:${AUTH}/`], { stdio: ['ignore', 'pipe', 'ignore'], timeout: 30000 }).toString();
  check(/<div id="result">NG<\/div>/.test(dom), 'headless Chromium sees NG');
  check(/<div id="ws">WS NG<\/div>/.test(dom), 'headless Chromium: WS NG', dom.match(/<div id="ws">[^<]*/)?.[0]);
  const plain = await fetch(`http://127.0.0.1:${AUTH}/`);
  check(plain.status === 401 && (await plain.text()).includes('>NG<'), 'a plain request gets 401 NG');
  const plainTicket = await fetch(`http://127.0.0.1:${AUTH}/ticket`);
  check(plainTicket.status === 401, 'a plain request for a ticket gets 401');
  const direct = await new Promise((res) => {
    const ws = new WebSocket(`ws://127.0.0.1:${AUTH}/ws`);
    ws.onopen = () => { res('open'); ws.close(); };
    ws.onerror = () => res('refused');
  });
  check(direct === 'refused', 'a WebSocket straight from outside Poca: refused');

  authPage.close();
  // Closing the window: Poca stops what it started, then exits.
  await box.send('Browser.close').catch(() => {});
} catch (e) {
  check(false, 'the run', e.stack || String(e));
  try { console.log('tabs:', (await targets()).map((t) => t.url).join(' ')); } catch {}
  // Through the window too, so Poca still stops what it started.
  await box?.send('Browser.close').catch(() => {});
}

const code = await Promise.race([pocaExit, sleep(20000).then(() => 'timeout')]);
check(code !== 'timeout', 'Poca exits when its window closes');
if (code === 'timeout') poca.kill('SIGKILL');
check(!(await portOpen(AUTH)) && !(await portOpen(CONTROL)), 'closing Poca stops auth-test and control');

const leftovers = fs.readdirSync(runtime).filter((f) => f.startsWith('poca-'));
check(leftovers.length === 0, "Poca's runtime directory is removed", leftovers.join(' '));
if (secret) {
  const holds = [];
  const walk = (d) => {
    for (const f of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, f.name);
      if (f.isDirectory()) walk(p);
      else if (f.isFile() && fs.readFileSync(p).includes(secret)) holds.push(p);
    }
  };
  walk(tmp);
  check(holds.length === 0, 'the secret is in no file (logs, config, runtime)', holds.join(' '));
}

const logText = fs.readdirSync(logs).map((f) => fs.readFileSync(path.join(logs, f), 'utf8')).join('');
check(logText.includes('GET /ws 101') && !logText.includes('ticket='), 'the logs show the handshakes but no ticket');

fs.rmSync(tmp, { recursive: true, force: true });
console.log(failures ? `${failures} failed` : 'all ok');
process.exit(failures ? 1 : 0);
