import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--warm-only', action='store_true')
parser.add_argument('--zooms', nargs='+', type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'; repo.mkdir()
def git(*args): return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Prefetch fixture'),
        ('config', 'user.email', 'prefetch@example.invalid'), ('config', 'commit.gpgsign', 'false')]: git(*command)
commits = {}
for index, subject in enumerate(['Root review', 'Warm target', 'Slow target', 'Current review']):
    (repo / 'a.cpp').write_text(f'int value = {index};\n')
    git('add', '.'); git('commit', '-qm', subject); commits[subject] = git('rev-parse', 'HEAD')
wrapper = out / 'bin'; wrapper.mkdir()
real_git = shutil.which('git')
(wrapper / 'git').write_text('#!/usr/bin/env python3\nimport os, sys, time\nfrom pathlib import Path\n'
    + f"slow = {commits['Slow target']!r}\n"
    + "if '--git-common-dir' in sys.argv and any(a.startswith(slow) for a in sys.argv):\n    folder = Path(os.environ['FH_PREFETCH_EVENTS'])\n    (folder / 'slow.started').touch()\n    time.sleep(8)\n    (folder / 'slow.finished').touch()\n"
    + f"os.execv({real_git!r}, [{real_git!r}, *sys.argv[1:]])\n")
(wrapper / 'git').chmod(0o755)
binary = args.binary.resolve(); digest = hashlib.sha256(binary.read_bytes()).hexdigest()
def capture(label, count):
    return f'workspace_checkpoint {count} {label}\nscreenshot {label}\n'
results = []
for zoom in args.zooms:
    directory = out / str(zoom); directory.mkdir()
    def replay(label, script):
        target = directory / label; target.mkdir()
        settings = target / 'settings'; settings.mkdir()
        path = target / 'journey.e2e'; path.write_text(script)
        with (target / 'run.log').open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', f'--test-script={path}',
                f'--screenshot-dir={target}', '--e2e-timeout=180'], cwd=ROOT,
                env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_NATIVE_HIDDEN='1', FH_TEST_SETTINGS_DIR=str(settings),
                    FH_PREFETCH_EVENTS=str(target), PATH=os.environ['PATH'] if args.baseline or args.warm_only else str(wrapper) + ':' + os.environ['PATH']),
                stdout=log, stderr=subprocess.STDOUT, timeout=210)
        assert result.returncode == 0, target / 'run.log'
        assert 'Native test window hidden=1 key=0' in (target / 'run.log').read_text()
        return target
    setup = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    setup += 'screenshot ready\nclick_text "Current review"\nkey ENTER\nwait_for_refresh\nwait_frames 15\nscreenshot current\n'
    pilot = replay('pilot', setup)
    layout = json.loads((pilot / 'current.json').read_text())
    nodes = {n['id']: n for n in layout['nodes']}
    def point(subject):
        node = next(n for n in nodes.values() if n['rendered'] and n.get('name') == 'commit_subject' and n.get('text') == subject)
        while node.get('name') != 'commit_row': node = nodes[node['parent']]
        r = node['visible_rect']; assert r['height'] > 0
        return f"{r['x'] + r['width'] * .5} {r['y'] + r['height'] * .5}"
    script = setup
    if not args.baseline:
        script += f"mouse_move {point('Warm target')}\n" + capture('hover_started', 2)
        script += 'wait_commit_prefetch 1\n' + capture('warmed', 2)
    script += f"reading_probe warm_click review - {commits['Warm target']}\nclick_text \"Warm target\"\nreading_checkpoint\n"
    script += 'wait_for_refresh\n' + capture('opened', 3)
    if not args.baseline and not args.warm_only:
        script += f"mouse_move {point('Slow target')}\nwait_for_path slow.started\n" + capture('pending', 3)
        for subject in ['Root review', 'Current review', 'Warm target']:
            script += f'mouse_move {point(subject)}\n'
        script += 'mouse_move 0 0\nwait_frames 30\n' + capture('cancelled', 3)
        script += f"mouse_move {point('Root review')}\nwait_commit_prefetch 2\n" + capture('next_warmed', 3)
        script += 'key DOWN\nwait_for_refresh\nwait_frames 40\n' + capture('keyboard', 3)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    run = replay('run', script)
    probe = json.loads((run / 'warm_click.reading.json').read_text())
    assert probe['patch_cache']['bytes'] <= 32 * 1024 * 1024
    if args.baseline: assert probe['patch_cache']['miss_delta'] == 1
    else:
        assert probe['patch_cache']['hit_delta'] == 1 and probe['patch_cache']['miss_delta'] == 0, probe
        def state(label): return json.loads((run / f'{label}.workspace.json').read_text())
        warmed = state('warmed'); assert warmed['prefetch']['submitted'] == warmed['prefetch']['completed'] == 1
        assert warmed['active'] == 2 and len(warmed['history']) == len(state('hover_started')['history'])
        if not args.warm_only:
            assert state('pending')['prefetch']['submitted'] == 2
            assert state('cancelled')['prefetch']['cancelled'] == 1
            next_warmed = state('next_warmed')['prefetch']
            assert next_warmed['submitted'] == 3 and next_warmed['completed'] == 2 and not next_warmed['pending'], next_warmed
            keyboard = state('keyboard')
            assert keyboard['prefetch']['submitted'] == 3 and not keyboard['prefetch']['pending'], keyboard['prefetch']
            active = next(t for t in keyboard['tabs'] if t['id'] == keyboard['active'])
            assert active['revision'] == commits['Root review']
            assert not (run / 'slow.finished').exists()
    results.append(dict(zoom=zoom, probe=probe))
    print(f'PASS {zoom}% ' + ('cold baseline' if args.baseline else 'stable hover, cached click, cancellation, rapid traversal and keyboard foreground priority'), flush=True)
assert not git('status', '--porcelain')
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, warm_only=args.warm_only, binary_sha256=digest, results=results), indent=2) + '\n')
