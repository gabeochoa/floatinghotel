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
paths = ['chain/one/two/three/leaf.cpp', 'other/ui/components/a.cpp',
         'src/tools/tool.cpp', 'src/ui/components/a.cpp', 'src/ui/components/b.cpp']
for name in paths:
    p = repo / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('int value = 1;\n')
def git(*args): return subprocess.check_output(['git', '-C', str(repo), *args])
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Compact tree fixture'),
                ('config', 'user.email', 'compact@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Compact directories')]: git(*command)
for name in paths: (repo / name).write_text('int value = 2;\n')
initial = git('diff')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
geometry = {}
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name): return f'wait_frames 12\nworkspace_checkpoint 2 {name}\nscreenshot {name}\n'
    def query(text): return 'click_ui commit_file_filter\nkey CMD+A\nkey BACKSPACE\n' + (f'type "{text}"\n' if text else '') + 'wait_frames 6\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Compact directories"\nwait_for_refresh\nkey ENTER\n' + capture('initial')
    script += query('a.cpp') + capture('duplicates')
    script += 'click_ui jump_to_diff:other/ui/components/a.cpp\n' + capture('other_selected')
    script += query('src/ui/') + 'key TAB\n' + capture('filtered')
    script += 'key RIGHT\nkey DOWN\n' + capture('second_file')
    script += 'key LEFT\nkey LEFT\n' + capture('collapsed')
    script += 'key RIGHT\nkey RIGHT\nkey ENTER\n' + capture('kept')
    script += 'type "src/ui"\n' + capture('typed')
    script += 'key LEFT\n' + query('') + query('src/ui/') + capture('filter_preserved')
    script += 'key TAB\nkey RIGHT\nkey RIGHT\nclick_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:src/ui/components/a.cpp\n'
    script += 'native_menu_action "Review Workspace (toggle)"\nright_click_ui jump_to_diff:src/ui/components/a.cpp\nwait_frames 4\nclick_ui "context_menu_item_Reveal in tree"\n' + capture('working_tree')
    script += 'click_ui tree_directory:src/ui/components/\n' + capture('working_collapsed')
    script += 'click_ui tree_directory:src/ui/components/\nwait_frames 6\nkey RIGHT\n' + capture('working_expanded')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def layout(name): return json.loads((directory / f'{name}.json').read_text())
    def state(name): return json.loads((directory / f'{name}.workspace.json').read_text())
    def nodes(name): return [n for n in layout(name)['nodes'] if n['rendered']]
    def rows(name, prefix='commit_directory:'): return [n for n in nodes(name) if n.get('name', '').startswith(prefix)]
    def labels(name): return {n['text'] for n in nodes(name) if n.get('name') == 'tree_directory_name'}
    assert 'chain/one/two/three' in labels('initial'), zoom
    assert {'other/ui/components', 'src/ui/components'} <= labels('duplicates'), zoom
    duplicate_paths = {n['focus_target']['item'] for n in nodes('duplicates') if n.get('name') == 'commit_changed_file'}
    assert duplicate_paths == {'other/ui/components/a.cpp', 'src/ui/components/a.cpp'}, (zoom, duplicate_paths)
    current = state('other_selected')
    assert next(t for t in current['tabs'] if t['id'] == current['active'])['path'] == 'other/ui/components/a.cpp'
    for name in ['filtered', 'collapsed', 'filter_preserved']:
        assert labels(name) == {'src/ui/components'}, (zoom, name, labels(name))
        assert len(rows(name)) == 1 and rows(name)[0]['focus_target']['item'] == 'src/ui/components/'
    for name in ['collapsed', 'filter_preserved', 'working_collapsed']:
        assert not any(n.get('focus_target', {}).get('item') in ['src/ui/components/a.cpp', 'src/ui/components/b.cpp'] and n.get('name') in ['file_row', 'commit_changed_file'] for n in nodes(name)), (zoom, name)
    for name, path in [('filtered', 'src/ui/components/'), ('second_file', 'src/ui/components/b.cpp'), ('kept', 'src/ui/components/a.cpp'), ('typed', 'src/ui/components/')]:
        focused = [n for n in nodes(name) if n['focused']]
        assert len(focused) == 1 and focused[0]['focus_target']['item'] == path, (zoom, name, focused)
        assert focused[0]['visible_rect']['height'] >= focused[0]['rect']['height'] - 1, (zoom, name)
    geometry[str(zoom)] = {}
    for name in ['working_tree', 'working_expanded']:
        row = next(n for n in nodes(name) if n.get('name') == 'file_row' and n.get('focus_target', {}).get('item') == 'src/ui/components/a.cpp')
        assert row['visible_rect']['height'] >= row['rect']['height'] - 1, (zoom, name, row)
        geometry[str(zoom)][name] = row['visible_rect']
    assert git('diff') == initial and git('diff', '--cached') == b''
    print(f'PASS {zoom}%: compact paths, branches, filtering, duplicate names, keyboard and working-tree folds', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=geometry), indent=2) + '\n')
