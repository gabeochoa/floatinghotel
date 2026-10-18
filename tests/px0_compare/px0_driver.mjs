// Drives px0 in a dedicated headless Chrome profile over the DevTools protocol
// and times each journey the way floatinghotel's reading probe does: from the
// input event reaching the page to the first animation frame in which the
// correct content is visible (CPU completion, not presentation).
//
// node px0_driver.mjs --px0 BIN --repo DIR --out FILE [--reps N] [--zoom 100]
//                     [--port 7790] [--chrome PATH] [--width 1600 --height 1000]
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, value, i, all) =>
  (value.startsWith('--') ? [...pairs, [value.slice(2), all[i + 1]]] : pairs), []));
const reps = +(args.reps ?? 3), zoom = +(args.zoom ?? 100) / 100, port = +(args.port ?? 7790);
// Which files the journey uses and which optional steps apply. The default is
// the deterministic fixture; real repositories pass --config.
const { readFileSync: readConfig } = await import('node:fs');
const cfg = { a: 'alpha.cpp', b: 'beta.cpp', big: 'big.txt', readme: 'README.md', tree: true, find: true, search: true, extras: true,
  ...(args.config ? JSON.parse(readConfig(args.config, 'utf8')) : {}) };
// Line and text a destination must show: the file's first non-blank line,
// cut at the first tab so rendered tab stops cannot break the match.
const firstLine = path => {
  const lines = readConfig(join(args.repo, path), 'utf8').split('\n');
  const n = lines.findIndex(l => l.trim());
  return { line: n + 1, text: lines[n].trim().split('\t')[0].slice(0, 30) };
};
const width = +(args.width ?? 1600), height = +(args.height ?? 1000);
const chromePath = args.chrome ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = ms => new Promise(r => setTimeout(r, ms));
// A stalled launch must fail, not hang the whole run.
setTimeout(() => { console.error('px0_driver watchdog: no result after 15 minutes'); process.exit(3); }, 15 * 60 * 1000).unref();
const get = (u, ms = 2000) => fetch(u, { signal: AbortSignal.timeout(ms) });
const started = Date.now();
const progress = what => console.error(`[px0_driver +${((Date.now() - started) / 1000).toFixed(1)}s] ${what}`);

async function until(fn, ms = 20000, what = 'condition') {
  const end = Date.now() + ms;
  for (;;) {
    try { const v = await fn(); if (v) return v; } catch {}
    if (Date.now() > end) throw new Error('timed out waiting for ' + what);
    await sleep(50);
  }
}

const px0 = spawn(args.px0, ['-no-open', '-no-lsp', '-quiet', '-port', String(port), args.repo], { stdio: 'ignore' });
const profile = mkdtempSync(join(tmpdir(), 'px0-chrome-'));
const devtools = port + 1000;
const chrome = spawn(chromePath, ['--headless=new', `--remote-debugging-port=${devtools}`, `--user-data-dir=${profile}`,
  '--no-first-run', '--no-default-browser-check', '--disable-extensions', '--disable-background-networking',
  `--window-size=${width},${height}`, 'about:blank'], { stdio: 'ignore' });
const cleanup = () => { try { chrome.kill('SIGKILL'); } catch {} try { px0.kill('SIGKILL'); } catch {} try { rmSync(profile, { recursive: true, force: true }); } catch {} };
process.on('exit', cleanup);

const url = `http://127.0.0.1:${port}/`;
await until(() => get(url).then(r => r.ok), 30000, 'px0');
progress('px0 serving');
const target = await until(() => get(`http://127.0.0.1:${devtools}/json/list`).then(r => r.json())
  .then(list => list.find(t => t.type === 'page')), 30000, 'chrome');
progress('chrome target ready');
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((ok, fail) => { ws.onopen = ok; ws.onerror = fail; });
let nextId = 1; const pending = new Map();
ws.onmessage = event => { const m = JSON.parse(event.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
const send = (method, params = {}) => new Promise((ok, fail) => {
  const id = nextId++;
  pending.set(id, m => (m.error ? fail(new Error(method + ': ' + m.error.message)) : ok(m.result)));
  ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async (expression, awaitPromise = false) => {
  const r = await send('Runtime.evaluate', { expression, awaitPromise, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.text + ' ' + (r.exceptionDetails.exception?.description ?? ''));
  return r.result.value;
};

// Browser zoom: CSS viewport shrinks by the zoom factor, device pixels grow by it.
await send('Emulation.setDeviceMetricsOverride', { width: Math.round(width / zoom), height: Math.round(height / zoom), deviceScaleFactor: zoom, mobile: false });
await send('Page.enable');
await send('Page.navigate', { url });
await until(() => evaluate(`!!document.querySelector('#tree [data-file]')`), 30000, 'px0 tree');
progress('page loaded');

// Page-side probe: t0 is stamped by a capturing listener when the armed input
// arrives; each animation frame checks the predicate.
await evaluate(`(() => {
  window.__probe = { t0: null, armed: false };
  for (const type of ['keydown', 'mousedown', 'beforeinput']) addEventListener(type, () => {
    if (window.__probe.armed && window.__probe.t0 === null) window.__probe.t0 = performance.now();
  }, true);
  window.__visibleRow = (path, line, text) => {
    const tab = document.querySelector('#tabs .tab.active');
    if (!tab || tab.title !== path) return false;
    const row = document.querySelector('#rows .row[data-l="' + line + '"]');
    if (!row || !row.textContent.includes(text)) return false;
    const r = row.getBoundingClientRect(), v = document.querySelector('#viewport').getBoundingClientRect();
    return r.height > 0 && r.top >= v.top && r.bottom <= v.bottom;
  };
  window.__arm = predicate => new Promise(resolve => {
    window.__probe = { t0: null, armed: true };
    const started = performance.now();
    const tick = () => {
      const p = window.__probe;
      if (p.t0 !== null && predicate()) { p.armed = false; resolve({ ms: performance.now() - p.t0 }); return; }
      if (performance.now() - started > 20000) { p.armed = false; resolve({ error: 'timeout' }); return; }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  });
})()`);

const MOD = 4, ALT = 1, CTRL = 2, SHIFT = 8;  // CDP modifier bits; Mod is Cmd on a Mac page
const codes = { Enter: 13, Escape: 27, ArrowLeft: 37, ArrowRight: 39, ArrowDown: 40, Tab: 9, Home: 36 };
async function key(keyName, modifiers = 0, code) {
  const keyCode = codes[keyName] ?? keyName.toUpperCase().charCodeAt(0);
  const base = { modifiers, key: keyName, code: code ?? (codes[keyName] ? keyName : 'Key' + keyName.toUpperCase()), windowsVirtualKeyCode: keyCode };
  await send('Input.dispatchKeyEvent', { type: 'keyDown', ...base });
  await send('Input.dispatchKeyEvent', { type: 'keyUp', ...base });
}
const type = text => send('Input.insertText', { text });
async function click(selector) {
  const box = await until(() => evaluate(`(() => { const e = document.querySelector(${JSON.stringify(selector)});
    if (!e) return null; e.scrollIntoView({ block: 'center' }); const r = e.getBoundingClientRect();
    return r.width && r.height ? { x: r.x + r.width / 2, y: r.y + r.height / 2 } : null; })()`), 20000, selector);
  for (const t of ['mousePressed', 'mouseReleased'])
    await send('Input.dispatchMouseEvent', { type: t, x: box.x, y: box.y, button: 'left', clickCount: 1 });
}

// One measured step: arm the probe, perform the input, wait for the frame.
async function step(name, predicate, act) {
  const done = evaluate(`window.__arm(() => ${predicate})`, true);
  await sleep(20);
  await act();
  const r = await done;
  // A timeout is a result to publish, not a reason to drop the launch.
  if (r.error) { progress(`${name}: ${r.error}`); return { step: name, ms: null, error: r.error }; }
  return { step: name, ms: r.ms };
}

const row = (path, line, text) => `window.__visibleRow(${JSON.stringify(path)}, ${line}, ${JSON.stringify(text)})`;
// Enter takes the first result, so wait until the debounced list shows the file.
const quickOpen = async path => {
  await key('p', MOD);
  await until(() => evaluate(`!document.querySelector('#overlay')?.hidden`), 20000, 'palette');
  // Drop the previous query's results so only fresh ones can satisfy the wait.
  await sleep(200);
  await evaluate(`document.querySelector('#pal-list').innerHTML = ''`);
  await type(path);
  // Fuzzy ranking can put another file first (docs/changes.rst above
  // CHANGES.rst); find the exact path and move the selection onto it.
  const name = path.split('/').pop(), dir = path.includes('/') ? path.slice(0, path.lastIndexOf('/')) : '';
  const index = await until(() => evaluate(`(() => {
    const items = [...document.querySelectorAll('#pal-list .pi')];
    const i = items.findIndex(it => it.querySelector('.pn')?.textContent === ${JSON.stringify(name)} &&
      (it.querySelector('.pp')?.textContent || '') === ${JSON.stringify(dir)});
    return i >= 0 ? i + 1 : 0; })()`), 20000, 'palette result ' + path).catch(async error => {
    progress('palette: ' + await evaluate(`[...document.querySelectorAll('#pal-list .pi')].slice(0, 5).map(it =>
      JSON.stringify([it.querySelector('.pn')?.textContent, it.querySelector('.pp')?.textContent])).join(' ')`));
    throw error;
  });
  for (let i = 1; i < index; i++) await key('ArrowDown', 0, 'ArrowDown');
  await until(() => evaluate(`document.querySelector('#pal-list .pi.sel .pn')?.textContent === ${JSON.stringify(name)}`), 5000, 'selected ' + path);
};

// Memory journey: 20 tabs, a long scroll, close everything, 30 s idle. The
// runner samples process memory; marks let it split the timeline.
if (args.journey === 'memory') {
  const mark = name => console.log(JSON.stringify({ mark: name, t: Date.now() }));
  console.log(JSON.stringify({ pids: { px0: px0.pid, chrome: chrome.pid } }));
  mark('start');
  for (let i = 0; i < 20; i++) {
    const path = `src/module_${String(i).padStart(2, '0')}.cpp`;
    await quickOpen(path);
    await step('open_tab', row(path, 1, 'module header'), () => key('Enter'));
  }
  mark('tabs_open');
  await quickOpen('big.txt');
  await step('open_big', row('big.txt', 1, 'line 000001'), () => key('Enter'));
  for (let i = 0; i < 200; i++) {
    await send('Input.dispatchMouseEvent', { type: 'mouseWheel', x: 800 / zoom, y: 500 / zoom, deltaX: 0, deltaY: 4000 });
    await sleep(10);
  }
  await sleep(500);
  mark('scrolled');
  for (let i = 0; i < 30 && await evaluate(`document.querySelectorAll('#tabs .tab').length`); i++) { await key('w', ALT, 'KeyW'); await sleep(30); }
  mark('closed');
  await sleep(30000);
  mark('idle');
  ws.close();
  cleanup();
  process.exit(0);
}

// Stress journey: the cases floatinghotel's stress.py checks, where px0 has an
// equivalent. Mutates files, so give it its own fixture copy.
if (args.journey === 'stress') {
  const { readFileSync, writeFileSync: write, statSync, utimesSync } = await import('node:fs');
  const results = {};
  const check = (name, ok, detail) => { results[name] = { pass: !!ok, detail }; progress(`${ok ? 'PASS' : 'FAIL'} ${name} ${detail}`); };
  const activeTab = () => evaluate(`document.querySelector('#tabs .tab.active')?.title || ''`);
  const visibleRows = () => evaluate(`[...document.querySelectorAll('#rows .row')].filter(r => {
    const b = r.getBoundingClientRect(), v = document.querySelector('#viewport').getBoundingClientRect();
    return b.height > 0 && b.bottom > v.top && b.top < v.bottom; }).map(r => r.textContent)`);
  const open = async path => { await quickOpen(path); await key('Enter'); };
  // Rapid A/B/A.
  await open('alpha.cpp'); await open('beta.cpp'); await open('alpha.cpp');
  await sleep(1500);
  const rapid = await visibleRows();
  check('rapid_aba', (await activeTab()) === 'alpha.cpp' && rapid.some(t => t.includes('alpha_1 = 1')) && !rapid.some(t => t.includes('beta_1')),
    'alpha.cpp active with its own rows');
  // Superseded palette query.
  await key('p', MOD);
  await until(() => evaluate(`!document.querySelector('#overlay')?.hidden`));
  await type('module_03'); await evaluate(`document.querySelector('#pal').value = ''`); await type('search_target');
  await until(() => evaluate(`(document.querySelector('#pal-list .pi')?.textContent || '').includes('search_target')`), 20000, 'superseded');
  await key('Enter'); await sleep(1000);
  check('superseded_query', (await activeTab()) === 'src/search_target.cpp', 'final query opened');
  // Close while loading: px0 creates a tab only once its first chunk has
  // arrived, so there is no loading document to close. Recorded, not scored.
  await quickOpen('big.txt'); await key('Enter'); await key('w', ALT, 'KeyW');
  await open('gamma.cpp'); await sleep(1000);
  const tabs = await evaluate(`[...document.querySelectorAll('#tabs .tab')].map(t => t.title)`);
  results.close_during_load = { pass: null, detail: `not applicable: no tab exists while loading; the close hit the previous tab, then big.txt opened (tabs: ${tabs.join(', ')})` };
  for (let i = 0; i < 30 && await evaluate(`document.querySelectorAll('#tabs .tab').length`); i++) { await key('w', ALT, 'KeyW'); await sleep(20); }
  // Changed between pages: first chunk loaded, file rewritten, then far chunks.
  await open('big.txt'); await until(() => evaluate(row('big.txt', 1, 'line 000001')), 20000, 'big first page');
  const big = join(args.repo, 'big.txt');
  const original = readFileSync(big);
  write(big, original.toString().replace(/^line /gm, 'LINE '));
  for (let i = 0; i < 40; i++) { await send('Input.dispatchMouseEvent', { type: 'mouseWheel', x: 800, y: 500, deltaX: 0, deltaY: 40000 }); await sleep(40); }
  await sleep(1500);
  const far = await visibleRows();
  await key('Home', MOD, 'Home'); await sleep(1000);
  const top = await visibleRows();
  const mixed = far.some(t => t.includes('LINE ')) && top.some(t => t.includes('line '));
  check('changed_between_pages', !mixed, `far rows rewritten: ${far.some(t => t.includes('LINE '))}; top rows original: ${top.some(t => t.includes('line '))}`);
  write(big, original);
  // Same size, same mtime, reopened.
  await open('alpha.cpp');
  await until(() => evaluate(row('alpha.cpp', 1, 'alpha_1 = 1')), 20000, 'alpha before rewrite');
  await key('w', ALT, 'KeyW');
  await until(async () => !(await evaluate(`[...document.querySelectorAll('#tabs .tab')].some(t => t.title === 'alpha.cpp')`)), 20000, 'alpha closed');
  const alphaPath = join(args.repo, 'alpha.cpp');
  const st = statSync(alphaPath);
  const alpha = readFileSync(alphaPath);
  write(alphaPath, alpha.toString().replace('int alpha_1 = 1;', 'int alpha_1 = 9;'));
  utimesSync(alphaPath, st.atime, st.mtime);
  await open('alpha.cpp');
  await sleep(1500);
  progress('after reopen: active=' + await activeTab() + ' tabs=' + await evaluate(`[...document.querySelectorAll('#tabs .tab')].map(t => t.title).join(',')`) +
    ' status=' + await evaluate(`document.querySelector('#status')?.textContent.slice(0, 200)`));
  await until(() => evaluate(`document.querySelector('#tabs .tab.active')?.title === 'alpha.cpp'`), 20000, 'alpha reopened');
  await sleep(1000);
  const same = await visibleRows();
  check('same_size_same_mtime', same.some(t => t.includes('alpha_1 = 9')), 'rewritten bytes visible after reopen');
  write(alphaPath, alpha); utimesSync(alphaPath, st.atime, st.mtime);
  // Reopen after churn (px0 evicts on close).
  for (let i = 0; i < 20; i++) await open(`src/module_${String(i).padStart(2, '0')}.cpp`);
  for (let i = 0; i < 30 && await evaluate(`document.querySelectorAll('#tabs .tab').length`); i++) { await key('w', ALT, 'KeyW'); await sleep(20); }
  await open('beta.cpp'); await sleep(1000);
  check('evicted_reopen', (await visibleRows()).some(t => t.includes('beta_1 = 1')), 'beta.cpp content after churn');
  writeFileSync(args.out, JSON.stringify({ app: 'px0', results, passed: Object.values(results).every(r => r.pass !== false) }, null, 1));
  ws.close(); cleanup(); process.exit(0);
}

const samples = [];
for (let rep = 1; rep <= reps; rep++) {
  const temperature = rep === 1 ? 'cold' : 'warm';
  progress(`rep ${rep}`);
  const record = s => samples.push({ app: 'px0', rep, temperature, zoom: zoom * 100, ...s });
  const A = firstLine(cfg.a), B = firstLine(cfg.b), BIG = firstLine(cfg.big);
  const atA = row(cfg.a, A.line, A.text), atB = row(cfg.b, B.line, B.text);
  if (cfg.tree) record(await step('tree_open', atA, () => click(`#tree [data-file="${cfg.a}"]`)));
  else { await quickOpen(cfg.a); record(await step('quick_open_a', atA, () => key('Enter'))); }
  await quickOpen(cfg.b);
  record(await step('quick_open', atB, () => key('Enter')));
  // Ctrl+Tab: with two tabs, px0's next-tab and floatinghotel's most-recent
  // switcher both alternate, so the same keystroke drives both apps.
  record(await step('tab_a', atA, () => key('Tab', CTRL, 'Tab')));
  record(await step('tab_b', atB, () => key('Tab', CTRL, 'Tab')));
  record(await step('tab_a_again', atA, () => key('Tab', CTRL, 'Tab')));
  record(await step('back', atB, () => key('ArrowLeft', ALT)));
  if (cfg.extras) record(await step('forward', atA, () => key('ArrowRight', ALT)));
  await quickOpen(cfg.big);
  record(await step('open_big', row(cfg.big, BIG.line, BIG.text), () => key('Enter')));
  // Go to a distant line through each app's go-to-line: an unloaded page.
  const gotoLine = async line => {
    await key('g', MOD);
    await until(() => evaluate(`!document.querySelector('#overlay')?.hidden`), 20000, 'line palette');
    await type(String(line));
    await until(() => evaluate(`(document.querySelector('#pal-list .pi.sel .pn')?.textContent || '').includes('${line}')`), 20000, 'line item');
  };
  if (cfg.extras) {
    await gotoLine(300000);
    record(await step('goto_line_far', row(cfg.big, 300000, 'line 300000'), () => key('Enter')));
    // A page deep inside a block comment: first text, then the comment colour.
    await quickOpen('comment.cpp');
    await key('Enter');
    await until(() => evaluate(`document.querySelector('#tabs .tab.active')?.title === 'comment.cpp'`), 20000, 'comment.cpp');
    await gotoLine(59990);
    const timing = evaluate(`new Promise(resolve => {
      window.__probe = { t0: null, armed: true };
      let text = null; const started = performance.now();
      const tick = () => {
        const p = window.__probe;
        const r = document.querySelector('#rows .row[data-l="59990"]');
        if (p.t0 !== null && r && r.textContent.includes('still comment 59990')) {
          if (text === null) text = performance.now() - p.t0;
          if (r.querySelector('.c i.c')) { p.armed = false; resolve({ text, syntax: performance.now() - p.t0 }); return; }
        }
        if (performance.now() - started > 20000) { p.armed = false; resolve({ text, syntax: null }); return; }
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick); })`, true);
    await sleep(20);
    await key('Enter');
    const t = await timing;
    record({ step: 'far_page_text', ms: t.text, error: t.text === null ? 'timeout' : undefined });
    record({ step: 'far_page_syntax', ms: t.syntax, error: t.syntax === null ? 'timeout' : undefined });
  }
  if (cfg.find) {
    await quickOpen('find.txt');
    await key('Enter');
    await until(() => evaluate(`document.querySelector('#tabs .tab.active')?.title === 'find.txt'`));
    await key('f', MOD);
    await until(() => evaluate(`!document.querySelector('#findbar').hidden`));
    await evaluate(`document.querySelector('#find-input').value = ''`);
    // Both apps find as you type; time from the last keystroke to the match on
    // screen, after the rest of the query is already in.
    await type('needle_12000');
    // The prefix matches the decoy on line 100; wait until px0 has jumped there.
    await until(() => evaluate(row('find.txt', 100, 'decoy')), 20000, 'decoy match');
    record(await step('find_later_page', row('find.txt', 120000, 'needle_120000'), () => type('0')));
    await key('Escape');
  }
  if (cfg.search) {
    // px0 0.1.1 removed its search-in-files input (6d5a60a) but kept the
    // shortcut, so the UI journey is unreachable. Time the server search the
    // input used to call instead; it is not comparable to a UI journey.
    record({ step: 'search_api', server_only: true, ms: await evaluate(`(async () => { const t = performance.now();
      const r = await fetch('/api/search?q=unique_search_token_42').then(r => r.json());
      if (!r.results?.length || r.results[0].matches[0].line !== 77) throw new Error('search missed');
      return performance.now() - t; })()`, true) });
  }
  if (cfg.readme) {
    await quickOpen(cfg.readme);
    await key('Enter');
    await until(() => evaluate(`document.querySelector('#tabs .tab.active')?.title === ${JSON.stringify(cfg.readme)}`));
    record(await step('markdown_preview',
      `(() => { const v = document.querySelector('#mdview'); const first = document.querySelector('#md > *');
        return v && !v.hidden && first && first.getBoundingClientRect().height > 0; })()`,
      async () => { const mdOn = await evaluate(`!document.querySelector('#mdview').hidden`);
        if (mdOn) await key('m', ALT, 'KeyM'); await key('m', ALT, 'KeyM'); }));
  }
  // Close every tab so the next repetition reopens rather than switches.
  for (let i = 0; i < 12 && await evaluate(`document.querySelectorAll('#tabs .tab').length`); i++) {
    await key('w', ALT, 'KeyW'); await sleep(30);
  }
}

// 1,000 animation frames on the 400k-line file while it scrolls. The page can
// see frame intervals, not CPU time per frame, so these are not comparable to
// floatinghotel's CPU frame times and are reported separately.
await quickOpen(cfg.big);
await key('Enter');
{ const BIG = firstLine(cfg.big); await until(() => evaluate(row(cfg.big, BIG.line, BIG.text)), 20000, cfg.big); }
const frameDone = evaluate(`new Promise(resolve => { const d = []; let last = performance.now();
  const tick = () => { const now = performance.now(); d.push(now - last); last = now;
    if (d.length < 1000) requestAnimationFrame(tick); else resolve(d); };
  requestAnimationFrame(tick); })`, true);
let scrolling = true;
frameDone.then(() => { scrolling = false; });
while (scrolling) {
  await send('Input.dispatchMouseEvent', { type: 'mouseWheel', x: 800 / zoom, y: 500 / zoom, deltaX: 0, deltaY: 2000 });
  await sleep(50);
}
const intervals = (await frameDone).slice(1).sort((a, b) => a - b);
const frames = { kind: 'rAF interval while scrolling', n: intervals.length,
  p50: intervals[Math.floor(intervals.length * 0.5)], p99: intervals[Math.floor(intervals.length * 0.99)], max: intervals[intervals.length - 1] };
const rss = await evaluate(`performance.memory ? performance.memory.usedJSHeapSize : 0`);
writeFileSync(args.out, JSON.stringify({ app: 'px0', zoom: zoom * 100, width, height, reps, pids: { px0: px0.pid, chrome: chrome.pid },
  js_heap_bytes: rss, frames, samples }, null, 1));
ws.close();
cleanup();
process.exit(0);
