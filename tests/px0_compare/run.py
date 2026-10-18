"""Repeatable floatinghotel vs px0 comparison runner.

Builds the deterministic fixture, records every pinned input (app commits and
binary hashes, Chrome, OS, machine, display scale, fonts), then runs both apps
in alternating blocks under `nice -n 10`, each launch a fresh process. Writes
every sample, failure and memory timeline, and a report whose head-to-head
verdict uses the journey declared below *before* any run.

python3 run.py --output DIR --px0 /path/to/px0 [--launches 30] [--reps 5]
               [--zoom-launches 3] [--chrome PATH]
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import pwd
import random
import statistics
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import fixture  # noqa: E402
import memsample  # noqa: E402

# Predeclared head-to-head journey: warm Quick Open at 100%. Both apps open a
# picker, take the same query and show the same file; neither has an advantage
# in how the step is driven. Chosen before the first full run.
PREDECLARED = ('quick_open', 'warm', 100, 'wide')
WIN_MARGIN = 0.20
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
# Pinned real repositories (shallow clones of release tags) and the files each
# journey uses: two ordinary sources and the largest text file under px0's
# 8 MiB search cap. Tree, Find and search stay on the fixture, where both
# apps can be given exactly matching work.
REAL_REPOS = {
    'flask': ('https://github.com/pallets/flask.git', '3.1.0',
              {'a': 'src/flask/helpers.py', 'b': 'src/flask/app.py', 'big': 'CHANGES.rst', 'readme': 'README.md'}),
    'redis': ('https://github.com/redis/redis.git', '7.4.1',
              {'a': 'src/networking.c', 'b': 'src/server.c', 'big': 'src/server.c', 'readme': 'README.md'}),
    'react': ('https://github.com/facebook/react.git', 'v19.0.0',
              {'a': 'packages/react/src/ReactHooks.js', 'b': 'packages/react-dom/src/client/ReactDOMRoot.js',
               'big': 'packages/react-reconciler/src/ReactFiberWorkLoop.js', 'readme': 'README.md'}),
    'typescript': ('https://github.com/microsoft/TypeScript.git', 'v5.7.2',
                   {'a': 'src/compiler/scanner.ts', 'b': 'src/compiler/parser.ts', 'big': 'src/compiler/checker.ts', 'readme': 'README.md'}),
}


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def sh(*args, cwd=None):
    try:
        return subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=60).stdout.strip()
    except Exception as error:  # metadata is best effort; record why
        return f'unavailable: {error}'


def metadata(px0, chrome, fixture_commit):
    px0_root = Path(px0).resolve().parent
    return {
        'recorded': datetime.now(timezone.utc).isoformat(),
        'floatinghotel': {'commit': sh('git', 'rev-parse', 'HEAD', cwd=ROOT), 'dirty': bool(sh('git', 'status', '--porcelain', cwd=ROOT)),
                          'binary_sha256': sha256(ROOT / 'output/floatinghotel.exe'), 'build': 'make (-O2, UBSan off)',
                          'mode': 'headless offscreen Metal, --test-mode'},
        'px0': {'commit': sh('git', 'rev-parse', 'HEAD', cwd=px0_root), 'version': sh(px0, '-version'),
                'binary_sha256': sha256(px0), 'flags': '-no-open -no-lsp -quiet'},
        'chrome': {'version': sh(chrome, '--version'), 'mode': 'headless=new, dedicated temporary profile'},
        'node': sh('node', '--version'),
        'os': {'platform': platform.platform(), 'macos': sh('sw_vers', '-productVersion')},
        'machine': {'model': sh('sysctl', '-n', 'hw.model'), 'cpu': sh('sysctl', '-n', 'machdep.cpu.brand_string'),
                    'cores': os.cpu_count(), 'memory_bytes': sh('sysctl', '-n', 'hw.memsize')},
        'display': {'note': 'both apps headless; floatinghotel offscreen at scale 1, px0 viewport emulated at deviceScaleFactor = zoom',
                    'main_display': sh('bash', '-c', "system_profiler SPDisplaysDataType | grep -E 'Resolution|Retina|UI Looks' | head -4")},
        'fonts': {'floatinghotel_code_px': 'settings default (Settings::kDefaultCodeFontSize)', 'px0': 'its stylesheet default'},
        'fixture_commit': fixture_commit,
        'priority': 'nice -n 10, run sequentially, one app at a time',
    }


def percentile(values, q):
    values = sorted(values)
    if not values:
        return None
    k = (len(values) - 1) * q
    lo, hi = int(k), min(int(k) + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (k - lo)


def bootstrap(values, stat, rounds=2000, seed=7):
    rng = random.Random(seed)
    estimates = sorted(stat([rng.choice(values) for _ in values]) for _ in range(rounds))
    return estimates[int(0.025 * rounds)], estimates[int(0.975 * rounds)]


def summarize(samples):
    groups, failed = {}, {}
    for s in samples:
        key = (s['app'], s['step'], s['temperature'], s['zoom'], s.get('window', 'wide') if not s.get('repo') else s['repo'])
        if s.get('ms') is None:
            failed[key] = failed.get(key, 0) + 1
            continue
        groups.setdefault(key, []).append(s['ms'])
    rows = []
    for key in sorted(set(failed) - set(groups)):
        rows.append({'app': key[0], 'step': key[1], 'temperature': key[2], 'zoom': key[3], 'window': key[4], 'n': 0,
                     'failed': failed[key]})
    for (app, step, temperature, zoom, window), values in sorted(groups.items()):
        p95 = lambda v: percentile(v, 0.95)
        lo, hi = bootstrap(values, p95) if len(values) >= 5 else (None, None)
        rows.append({'app': app, 'step': step, 'temperature': temperature, 'zoom': zoom, 'window': window, 'n': len(values),
                     'failed': failed.get((app, step, temperature, zoom, window), 0),
                     'p50': percentile(values, 0.5), 'p95': p95(values), 'max': max(values), 'p95_ci': [lo, hi]})
    return rows


def browser_env():
    # Chrome stalls loading a page when HOME is not the real home (keychain),
    # which is what a sandboxed caller sets for git. Give the browser side the
    # account's own home.
    return dict(os.environ, HOME=pwd.getpwuid(os.getuid()).pw_dir)


def run_px0(px0, repo, out, reps, zoom, width, height, chrome, journey='navigation', config=None):
    cmd = ['nice', '-n', '10', 'node', str(HERE / 'px0_driver.mjs'), '--px0', px0, '--repo', str(repo), '--out', str(out),
           '--reps', str(reps), '--zoom', str(zoom), '--width', str(width), '--height', str(height), '--chrome', chrome,
           '--port', str(7790 + random.randint(0, 200))]
    if config:
        cmd += ['--config', str(config)]
    if journey == 'stress':
        cmd += ['--journey', 'stress']
        with Path(out).with_suffix('.log').open('w') as log:
            result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, text=True, timeout=900, env=browser_env())
        data = json.loads(Path(out).read_text()) if Path(out).exists() else {'results': {}}
        data['exit'] = result.returncode
        return data
    if journey == 'memory':
        cmd += ['--journey', 'memory']
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=browser_env())
        sampler, marks, lines = None, {}, []
        for line in proc.stdout:
            lines.append(line)
            try:
                message = json.loads(line)
            except ValueError:
                continue
            if 'pids' in message:
                sampler = memsample.Sampler(message['pids'].values()).start()
            if 'mark' in message:
                marks[message['mark']] = message['t'] / 1000
        code = proc.wait()
        return {'app': 'px0', 'exit': code, 'errors': [] if code == 0 else lines[-20:], 'marks': marks,
                'samples': sampler.stop() if sampler else []}
    with Path(out).with_suffix('.log').open('w') as log:
        result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, text=True, timeout=1800, env=browser_env())
    if result.returncode != 0:
        return {'app': 'px0', 'exit': result.returncode, 'errors': Path(out).with_suffix('.log').read_text().splitlines()[-20:], 'samples': []}
    return json.loads(Path(out).read_text())


def run_fh(repo, out, reps, zoom, width, height, journey='navigation', config=None):
    cmd = ['nice', '-n', '10', sys.executable, str(HERE / 'fh_driver.py'), '--binary', str(ROOT / 'output/floatinghotel.exe'),
           '--repo', str(repo), '--out', str(out), '--reps', str(reps), '--zoom', str(zoom), '--width', str(width),
           '--height', str(height), '--journey', journey] + (['--config', str(config)] if config else [])
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=3600, cwd=HERE)
    try:
        data = json.loads(Path(out).read_text())
    except (OSError, ValueError):
        data = {'app': 'floatinghotel', 'samples': []}
    data['exit'] = result.returncode
    data.setdefault('errors', [])
    if result.returncode:
        data['errors'] = data['errors'] or (result.stdout + result.stderr).splitlines()[-20:]
    return data


def memory_phases(run):
    marks, samples = run.get('marks', {}), run.get('samples', [])
    if not samples:
        return {}
    phases = {}
    for name, t in marks.items():
        # The px0 driver exits right after its idle mark, so the sample nearest
        # that mark can postdate the processes; take the last live one before it.
        live = [s for s in samples if s['t'] <= t + 0.5 and s['total_kb'] > 0]
        near = live[-1] if live else min(samples, key=lambda s: abs(s['t'] - t))
        phases[name] = {'total_mb': near['total_kb'] / 1024, 'processes': near['processes'],
                        'by_name_mb': {k: v / 1024 for k, v in near['by_name'].items()}}
    peak = max(samples, key=lambda s: s['total_kb'])
    phases['peak'] = {'total_mb': peak['total_kb'] / 1024, 'processes': peak['processes'],
                      'by_name_mb': {k: v / 1024 for k, v in peak['by_name'].items()}}
    return phases


def verdict(rows, samples):
    step, temperature, zoom, window = PREDECLARED
    pick = lambda app: [s['ms'] for s in samples if s.get('ms') is not None and not s.get('repo') and s['app'] == app and s['step'] == step and s['temperature'] == temperature
                        and s['zoom'] == zoom and s.get('window', 'wide') == window]
    fh, px = pick('floatinghotel'), pick('px0')
    if len(fh) < 5 or len(px) < 5:
        return {'journey': PREDECLARED, 'decided': False, 'reason': 'too few samples'}
    rng = random.Random(11)
    ratios = sorted(percentile([rng.choice(fh) for _ in fh], .95) / percentile([rng.choice(px) for _ in px], .95) for _ in range(2000))
    lo, hi = ratios[50], ratios[1949]
    ratio = percentile(fh, .95) / percentile(px, .95)
    return {'journey': PREDECLARED, 'fh_p95': percentile(fh, .95), 'px0_p95': percentile(px, .95), 'n': [len(fh), len(px)],
            'ratio': ratio, 'ratio_ci95': [lo, hi], 'win': hi < 1 - WIN_MARGIN,
            'rule': f'floatinghotel p95 at least {WIN_MARGIN:.0%} lower with the whole 95% CI of the ratio below {1 - WIN_MARGIN:.2f}'}


def report(result):
    """Markdown report: every journey, failures, memory, frames, verdict."""
    m, v = result['metadata'], result['verdict']
    fmt = lambda x: '-' if x is None else f'{x:.1f}'
    lines = ['# floatinghotel vs px0', '',
             f"Recorded {m['recorded']}. floatinghotel `{m['floatinghotel']['commit'][:9]}`"
             f"{' (dirty)' if m['floatinghotel']['dirty'] else ''} binary `{m['floatinghotel']['binary_sha256'][:12]}`; "
             f"px0 `{m['px0']['commit'][:9]}` {m['px0']['version']} binary `{m['px0']['binary_sha256'][:12]}`; {m['chrome']['version']}; "
             f"macOS {m['os']['macos']} on {m['machine']['model']} ({m['machine']['cpu']}). Fixture `{m['fixture_commit'][:9]}`.", '',
             'Times are milliseconds from the input reaching the app to the first frame showing the destination (CPU completion, '
             'not presentation). Warm samples are repetitions after the first in a launch; each launch is a fresh process.', '',
             '## Predeclared journey', '']
    if v.get('decided') is False:
        lines.append(f"Undecided: {v['reason']}.")
    else:
        lines.append(f"{' / '.join(map(str, v['journey']))}: floatinghotel p95 {fmt(v['fh_p95'])} ms vs px0 {fmt(v['px0_p95'])} ms "
                     f"(n = {v['n'][0]} / {v['n'][1]}); ratio {v['ratio']:.2f}, 95% CI {v['ratio_ci95'][0]:.2f}-{v['ratio_ci95'][1]:.2f}. "
                     f"Rule: {v['rule']}. **{'Win' if v['win'] else 'No win claimed'}.**")
    lines += ['', '## Every journey', '', '| app | step | temp | zoom | window | n | failed | p50 | p95 (95% CI) | max |',
              '|---|---|---|---:|---|---:|---:|---:|---:|---:|']
    for r in result['summary']:
        ci = r.get('p95_ci') or [None, None]
        lines.append(f"| {r['app']} | {r['step']} | {r['temperature']} | {r['zoom']} | {r['window']} | {r['n']} | {r.get('failed', 0)} | "
                     f"{fmt(r.get('p50'))} | {fmt(r.get('p95'))} ({fmt(ci[0])}-{fmt(ci[1])}) | {fmt(r.get('max'))} |")
    lines += ['', '## Failures and timeouts', '']
    lines += [f"- launch {f['index']} {f['app']} zoom {f['zoom']} {f['window']}: {'; '.join(map(str, (f['errors'] or [])[:3]))}"
              for f in result['failures']] or ['None.']
    lines += ['', '## Frames', '', 'floatinghotel: CPU frame time, 10 blocks of 100 frames each after a long scroll. '
              'px0: animation-frame interval while scrolling (the page cannot see CPU time per frame); not comparable.', '']
    by_app = {}
    for f in result['frames']:
        if f['zoom'] == 100 and f['window'] == 'wide':
            value = max(b['p99'] for b in f['frames']) if isinstance(f['frames'], list) else f['frames']['p99']
            by_app.setdefault(f['app'], []).append(value)
    for app, values in by_app.items():
        lines.append(f"- {app}: per-launch p99 {fmt(min(values))}-{fmt(max(values))} ms over {len(values)} launches")
    lines += ['', '## Stress correctness', '', '| case | floatinghotel | px0 |', '|---|---|---|']
    cases = sorted(set(result['stress'].get('floatinghotel', {})) | set(result['stress'].get('px0', {})))
    mark = lambda r: '-' if r is None else ('n/a: ' + r['detail'] if r['pass'] is None else ('pass' if r['pass'] else 'FAIL: ' + r['detail']))
    for case in cases:
        lines.append(f"| {case} | {mark(result['stress'].get('floatinghotel', {}).get(case))} | {mark(result['stress'].get('px0', {}).get(case))} |")
    if m.get('real_repositories'):
        lines += ['', '## Real repositories', '', 'Rows in the journey table whose window column names a repository come from these '
                  'pinned shallow clones (tree, Find and search excluded; see above).', '']
        for name, r in m['real_repositories'].items():
            lines.append(f"- {name} {r['tag']} `{r['commit'][:9]}`, {r['files']} files: a `{r['a']}`, b `{r['b']}`, large `{r['big']}`")
    lines += ['', '## Memory (RSS, 20 tabs, scroll, close, 30 s idle)', '',
              'Summed resident set of every process the app uses; shared pages are counted once per process, which '
              'overstates multi-process totals.', '', '| app | phase | total MB | processes | largest components |', '|---|---|---:|---:|---|']
    for run in result['memory']:
        for phase in ('start', 'tabs_open', 'scrolled', 'closed', 'idle', 'peak'):
            p = run['phases'].get(phase)
            if p:
                top = ', '.join(f'{k} {v:.0f}' for k, v in sorted(p['by_name_mb'].items(), key=lambda kv: -kv[1])[:4])
                lines.append(f"| {run['app']} | {phase} | {p['total_mb']:.1f} | {p['processes']} | {top} |")
        if run.get('footprint_idle_mb'):
            lines.append(f"| {run['app']} | idle footprint | {run['footprint_idle_mb']:.1f} | 1 | physical footprint incl. GPU |")
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--px0', required=True)
    parser.add_argument('--launches', type=int, default=30)
    parser.add_argument('--reps', type=int, default=5, help='repetitions per launch: 1 cold, the rest warm')
    parser.add_argument('--zoom-launches', type=int, default=3)
    parser.add_argument('--memory-runs', type=int, default=1)
    parser.add_argument('--chrome', default=CHROME)
    parser.add_argument('--repos-dir', type=Path, help='where pinned real repositories are cloned (reused if present)')
    parser.add_argument('--real-launches', type=int, default=5)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    repo = out / 'fixture'
    commit = fixture.build(repo)
    meta = metadata(args.px0, args.chrome, commit)
    meta['plan'] = {'launches_100': args.launches, 'reps_per_launch': args.reps, 'zoom_launches': args.zoom_launches,
                    'predeclared': PREDECLARED, 'win_margin': WIN_MARGIN}
    (out / 'metadata.json').write_text(json.dumps(meta, indent=1))
    samples, failures, launches, frames = [], [], [], []
    configs = [(100, 1600, 1000, 'wide')] * args.launches
    for zoom in (140, 200):
        configs += [(zoom, 1600, 1000, 'wide')] * args.zoom_launches
    configs += [(100, 900, 1000, 'narrow')] * args.zoom_launches
    for index, (zoom, width, height, window) in enumerate(configs):
        order = ['px0', 'floatinghotel'] if index % 2 == 0 else ['floatinghotel', 'px0']
        for app in order:
            path = out / 'runs' / f'{index:03d}-{app}-{zoom}-{window}.json'
            path.parent.mkdir(exist_ok=True)
            started = time.time()
            data = (run_px0(args.px0, repo, path, args.reps, zoom, width, height, args.chrome) if app == 'px0'
                    else run_fh(repo, path, args.reps, zoom, width, height))
            launches.append({'index': index, 'app': app, 'zoom': zoom, 'window': window, 'exit': data.get('exit', 0),
                             'seconds': time.time() - started})
            if data.get('exit') or data.get('errors'):
                failures.append({'index': index, 'app': app, 'zoom': zoom, 'window': window, 'errors': data.get('errors')})
            for s in data.get('samples', []):
                samples.append({**s, 'launch': index, 'window': window})
            if data.get('frames'):
                frames.append({'app': app, 'launch': index, 'zoom': zoom, 'window': window, 'frames': data['frames']})
            print(f'[{index + 1}/{len(configs)}] {app:13} zoom {zoom} {window:6} exit {data.get("exit", 0)} '
                  f'samples {len(data.get("samples", []))} ({time.time() - started:.0f}s)', flush=True)
    memory = []
    for run in range(args.memory_runs):
        for app in (['px0', 'floatinghotel'] if run % 2 == 0 else ['floatinghotel', 'px0']):
            path = out / 'memory' / f'{run}-{app}.json'
            path.parent.mkdir(exist_ok=True)
            data = (run_px0(args.px0, repo, path, 1, 100, 1600, 1000, args.chrome, journey='memory') if app == 'px0'
                    else run_fh(repo, path, 1, 100, 1600, 1000, journey='memory'))
            path.write_text(json.dumps(data))
            memory.append({'app': app, 'run': run, 'exit': data.get('exit'), 'phases': memory_phases(data),
                           'footprint_idle_mb': data.get('footprint_idle_mb')})
            print(f'memory {app} exit {data.get("exit")}', flush=True)
    real = {}
    if args.repos_dir:
        args.repos_dir.mkdir(parents=True, exist_ok=True)
        for name, (url, tag, cfg) in REAL_REPOS.items():
            path = args.repos_dir / name
            if not path.exists():
                subprocess.run(['git', 'clone', '-q', '--depth', '1', '--branch', tag, url, str(path)], check=True)
            sha = sh('git', '-C', str(path), 'rev-parse', 'HEAD')
            config = out / f'config-{name}.json'
            config.write_text(json.dumps({**cfg, 'tree': False, 'find': False, 'search': False, 'extras': False}))
            real[name] = {'url': url, 'tag': tag, 'commit': sha, 'files': int(sh('bash', '-c', f'git -C {path} ls-files | wc -l') or 0), **cfg}
            for index in range(args.real_launches):
                for app in (['px0', 'floatinghotel'] if index % 2 == 0 else ['floatinghotel', 'px0']):
                    run = out / 'real' / f'{name}-{index:02d}-{app}.json'
                    run.parent.mkdir(exist_ok=True)
                    data = (run_px0(args.px0, path, run, args.reps, 100, 1600, 1000, args.chrome, config=config) if app == 'px0'
                            else run_fh(path, run, args.reps, 100, 1600, 1000, config=config))
                    if data.get('exit') or data.get('errors'):
                        failures.append({'index': f'{name}-{index}', 'app': app, 'zoom': 100, 'window': 'wide', 'errors': data.get('errors')})
                    for sample in data.get('samples', []):
                        samples.append({**sample, 'launch': f'{name}-{index}', 'window': 'wide', 'repo': name})
                    print(f'real {name} [{index + 1}/{args.real_launches}] {app:13} exit {data.get("exit", 0)} samples {len(data.get("samples", []))}', flush=True)
    meta['real_repositories'] = real
    (out / 'metadata.json').write_text(json.dumps(meta, indent=1))
    stress = {}
    for app in ('floatinghotel', 'px0'):
        target = out / 'stress' / app
        target.parent.mkdir(exist_ok=True)
        if app == 'floatinghotel':
            result = subprocess.run(['nice', '-n', '10', sys.executable, str(HERE / 'stress.py'), '--output', str(target)],
                                    capture_output=True, text=True, timeout=1800, cwd=HERE)
            data = json.loads((target / 'stress.json').read_text()) if (target / 'stress.json').exists() else {'results': {}}
        else:
            copy = out / 'stress' / 'px0-fixture'
            fixture.build(copy)
            data = run_px0(args.px0, copy, out / 'stress' / 'px0.json', 1, 100, 1600, 1000, args.chrome, journey='stress')
        stress[app] = data.get('results', {})
        print(f'stress {app}: ' + ', '.join(f"{k} {'pass' if v['pass'] else 'n/a' if v['pass'] is None else 'FAIL'}" for k, v in stress[app].items()), flush=True)
    rows = summarize(samples)
    result = {'metadata': meta, 'launches': launches, 'failures': failures, 'summary': rows,
              'verdict': verdict(rows, samples), 'memory': memory, 'frames': frames, 'stress': stress}
    (out / 'samples.json').write_text(json.dumps(samples))
    (out / 'summary.json').write_text(json.dumps(result, indent=1))
    (out / 'report.md').write_text(report(result))
    print(json.dumps(result['verdict'], indent=1))


if __name__ == '__main__':
    main()
