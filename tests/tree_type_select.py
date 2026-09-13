import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
for name in ['src/alpha.cpp', 'src/alpine.cpp', 'src/beta.cpp', 'src/bytes.cpp', 'src/echo-é.cpp', 'zeta/alpha.cpp']:
    p = repo / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('int value = 1;\n')
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Type fixture'),
                ('config', 'user.email', 'type@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Typing fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
(repo / 'src/alpha.cpp').write_text('int value = 2;\n')
initial_diff = subprocess.check_output(['git', '-C', str(repo), 'diff'])
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
geometry = {}
for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    checks = {}
    def capture(name, path=None, count=2):
        if path is not None: checks[name] = path
        return f'wait_frames 4\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Typing fixture"\nwait_for_refresh\nclick_ui commit_file_filter\nkey TAB\nkey DOWN\nkey DOWN\nkey DOWN\nkey DOWN\nkey DOWN\nkey DOWN\nkey LEFT\n'
    script += 'type "a"\n' + capture('alpha', 'src/alpha.cpp')
    script += 'type "a"\n' + capture('alpine', 'src/alpine.cpp')
    script += 'type "a"\n' + capture('cycle', 'src/alpha.cpp')
    script += 'type "lpi"\n' + capture('prefix', 'src/alpine.cpp')
    script += 'type "x"\n' + capture('no_match', 'src/alpine.cpp')
    script += 'key UP\ntype "b"\n' + capture('beta', 'src/beta.cpp')
    script += 'type "b"\n' + capture('bytes', 'src/bytes.cpp')
    script += 'type "B"\n' + capture('uppercase', 'src/beta.cpp')
    script += 'key UP\ntype "e"\n' + capture('unicode', 'src/echo-é.cpp')
    script += 'key ENTER\n' + capture('kept', 'src/echo-é.cpp')
    script += 'type "c"\n' + capture('no_review_shortcut', 'src/echo-é.cpp')
    script += 'click_ui commit_file_filter\n' + capture('filter_before')
    script += 'type "alpha"\nkey ALT+LEFT\n' + capture('filter_after')
    script += 'key CMD+A\nkey BACKSPACE\nnative_menu_action "Review Workspace (toggle)"\nnative_menu_action "All Files View"\nclick_ui sidebar_working_files\nwait_frames 6\nclick_ui file_row\ntype "by"\nwait_for_refresh\n' + capture('working_source', 'src/bytes.cpp', 3)
    script += 'key ENTER\n' + capture('source_kept', 'src/bytes.cpp', 3)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=120'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=150)
    assert result.returncode == 0, directory / 'run.log'
    def state(name): return json.loads((directory / f'{name}.workspace.json').read_text())
    def nodes(name): return json.loads((directory / f'{name}.json').read_text())['nodes']
    geometry[str(zoom)] = {}
    for name, expected in checks.items():
        focused = [n for n in nodes(name) if n['focused'] and n['rendered'] and not n['hidden']]
        assert len(focused) == 1, (zoom, name, focused)
        n = focused[0]
        assert n['focus_target']['region'] == 'Tree' and n['focus_target']['item'] == expected, (zoom, name, n)
        assert n['visual_focus'] and n['visible_rect']['height'] >= n['rect']['height'] - 1, (zoom, name, n)
        document = state(name)
        assert next(t for t in document['tabs'] if t['id'] == document['active'])['path'] == expected, (zoom, name, document)
        assert not document['review']['composing'] and not document['review']['approved'], (zoom, name)
        assert not any(n['rendered'] and n.get('name') == 'repo_search_input' for n in nodes(name)), (zoom, name)
        geometry[str(zoom)][name] = {'focus': n['focus_target'], 'rect': n['rect'], 'visible': n['visible_rect']}
    for name in ['alpha', 'alpine', 'cycle', 'prefix', 'no_match', 'unicode', 'working_source']:
        current = state(name)
        assert next(t for t in current['tabs'] if t['id'] == current['active'])['preview'], (zoom, name)
    for name in ['kept', 'source_kept']:
        current = state(name)
        assert not next(t for t in current['tabs'] if t['id'] == current['active'])['preview'], (zoom, name)
    before, after = state('filter_before'), state('filter_after')
    assert before['active'] == after['active'] and before['history'] == after['history'], zoom
    focused = next(n for n in nodes('filter_after') if n['focused'])
    assert focused['text'] == 'alpha' and focused['focus_target']['control'] == 'commit_file_filter', (zoom, focused)
    print(f'PASS {zoom}%: prefix, cycling, Unicode filenames, collapsed descendants, filter editing, preview/keep', flush=True)
assert subprocess.check_output(['git', '-C', str(repo), 'diff', '--cached']) == b''
assert subprocess.check_output(['git', '-C', str(repo), 'diff']) == initial_diff
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=geometry), indent=2) + '\n')
