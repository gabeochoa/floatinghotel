import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()


def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'History keyboard'),
             ('config', 'user.email', 'history@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*args)
hashes = []
for i in range(24):
    (repo / 'reader.cpp').write_text(f'int revision = {i};\n')
    git('add', '.')
    git('commit', '-qm', f'History entry {i:02}')
    hashes.insert(0, git('rev-parse', 'HEAD').decode().strip())
(repo / 'reader.cpp').write_text('int revision = 24;\n')
initial_diff = git('diff')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
results = {}
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    checks = {}

    def capture(name, index, count=2, preview=True):
        checks[name] = (index, preview)
        return f'wait_for_refresh\nwait_frames 12\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'

    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "History entry 23"\n' + capture('clicked', 0)
    script += 'key DOWN\n' + capture('next', 1)
    script += 'key UP\n' + capture('back', 0)
    script += 'key DOWN\n' * 18 + capture('far', 18)
    script += 'key DOWN\nkey DOWN\nkey DOWN\nkey UP\nkey DOWN\nkey DOWN\n' + capture('rapid', 22)
    script += 'key A\nkey ENTER\n' + capture('kept', 22, preview=False)
    script += 'key DOWN\n' + capture('root', 23, 3)
    script += 'key DOWN\n' + capture('last', 23, 3)
    script += 'key UP\n' * 23 + capture('first', 0, 3)
    script += 'key UP\n' + capture('endpoint', 0, 3)
    script += 'click_ui commit_file_filter\ntype "reader"\nkey DOWN\nkey UP\nkey A\n'
    script += 'wait_frames 8\nworkspace_checkpoint 3 editing\nscreenshot editing\n'
    script += 'key CMD+P\nclick_ui file_picker_input\ntype "reader"\nkey DOWN\nkey UP\n'
    script += 'wait_frames 8\nworkspace_checkpoint 3 picker\nscreenshot picker\nkey ESCAPE\n'
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'

    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())

    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())

    def active(name):
        current = state(name)
        return next(t for t in current['tabs'] if t['id'] == current['active'])

    results[str(zoom)] = {}
    for name, (index, preview) in checks.items():
        current = state(name)
        selected = active(name)
        assert selected['revision'] == hashes[index] and selected['preview'] == preview, (zoom, name, selected)
        focused = [n for n in layout(name)['nodes'] if n['focused'] and n['rendered']]
        assert len(focused) == 1, (zoom, name, focused)
        row = focused[0]
        assert row['focus_target']['region'] == 'History' and row['focus_target']['item'] == hashes[index], (zoom, name, row)
        assert row['visual_focus'] and row['visible_rect']['height'] >= row['rect']['height'] - 1, (zoom, name, row)
        assert current['inactive_payloads_empty'], (zoom, name)
        results[str(zoom)][name] = {'hash': hashes[index], 'rect': row['rect'], 'visible': row['visible_rect']}
    assert any('revision = 1;' in r['text'] for r in layout('rapid')['reading_rows']), zoom
    for name in ['editing', 'picker']:
        focused = [n for n in layout(name)['nodes'] if n['focused'] and n['rendered']]
        assert len(focused) == 1 and focused[0]['focus_target']['control'] == ('commit_file_filter' if name == 'editing' else 'file_picker_input'), (zoom, name, focused)
        assert active(name)['revision'] == hashes[0], (zoom, name)
        assert state(name)['history_index'] == state('endpoint')['history_index'], (zoom, name)
    assert not next(t for t in state('root')['tabs'] if t['revision'] == hashes[22])['preview'], zoom
    assert git('diff', '--cached') == b'' and git('diff') == initial_diff
    print(f'PASS {zoom}%: history arrows preview, keep, scroll, endpoints, rapid reads, and text focus', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=results), indent=2) + '\n')
