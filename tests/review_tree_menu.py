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
for stem in ['a', 'old', 'gone']:
    (repo / f'{stem}.cpp').write_text(''.join(f'int {stem}_value_{i} = {i};\n' for i in range(40)))
def git(*args): return subprocess.check_output(['git', '-C', str(repo), *args])
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Menu fixture'),
                ('config', 'user.email', 'menu@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Original menu files')]: git(*command)
before = git('rev-parse', 'HEAD').decode().strip()
git('mv', 'old.cpp', 'new.cpp')
git('rm', '-q', 'gone.cpp')
for name in ['a.cpp', 'new.cpp']:
    p = repo / name
    p.write_text(p.read_text().replace('value_10 = 10', 'value_10 = 1000'))
git('commit', '-qam', 'Rename and delete menu files')
after = git('rev-parse', 'HEAD').decode().strip()
p = repo / 'a.cpp'
p.write_text(p.read_text().replace('value_20 = 20', 'value_20 = 2000'))
(repo / 'untracked').mkdir()
(repo / 'untracked/fresh.cpp').write_text('int fresh_working_value = 42;\n')
(repo / 'index.cpp').write_text('int staged_value = 7;\n')
git('add', 'index.cpp')
initial = git('diff'), git('diff', '--cached'), git('status', '--porcelain')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
geometry = {}
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count): return f'wait_frames 12\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    def menu(path, action): return f'right_click_ui jump_to_diff:{path}\nwait_frames 4\nclick_ui "context_menu_item_{action}"\nwait_for_refresh\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Rename and delete menu files"\nwait_for_refresh\nkey ENTER\nclick_ui jump_to_diff:a.cpp\nwait_frames 6\nright_click_ui jump_to_diff:new.cpp\n' + capture('historical_menu', 2)
    script += 'key ESCAPE\n' + capture('dismissed', 2)
    script += menu('new.cpp', 'Open source') + capture('renamed_source', 3)
    script += menu('gone.cpp', 'Open source') + capture('deleted_source', 3)
    script += menu('a.cpp', 'Open diff') + capture('other_diff', 3)
    script += 'click_text "Original menu files"\nwait_for_refresh\n' + capture('preview', 3)
    script += menu('gone.cpp', 'Keep open') + capture('kept', 3)
    script += menu('gone.cpp', 'Open source') + capture('root_source', 4)
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:a.cpp\nright_click_ui jump_to_diff:untracked/fresh.cpp\n' + capture('working_menu', 4)
    script += 'click_ui "context_menu_item_Open source"\nwait_for_refresh\n' + capture('working_source', 4)
    script += 'click_text "Back to diff"\nwait_for_refresh\nnative_menu_action "Review Workspace (toggle)"\n'
    script += menu('untracked/fresh.cpp', 'Reveal in tree') + capture('working_revealed', 4)
    script += 'click_ui sidebar_review\nclick_ui review_staged_changes\nwait_for_refresh\n' + capture('staged', 4)
    script += menu('index.cpp', 'Open source') + capture('index_source', 5)
    script += 'click_text "Back to diff"\nwait_for_refresh\n' + menu('index.cpp', 'Reveal in tree') + capture('index_revealed', 5)
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
    def active(name):
        current = state(name)
        return next(t for t in current['tabs'] if t['id'] == current['active'])
    for name, path, revision in [('renamed_source', 'old.cpp', before), ('deleted_source', 'gone.cpp', before),
                                 ('root_source', 'gone.cpp', before), ('working_source', 'untracked/fresh.cpp', ''),
                                 ('index_source', 'index.cpp', 'INDEX')]:
        tab = active(name)
        assert (tab['kind'], tab['path'], tab['revision']) == ('source', path, revision), (zoom, name, tab)
    for name, text in [('renamed_source', 'old_value_10 = 10;'), ('deleted_source', 'gone_value_'),
                       ('root_source', 'gone_value_'), ('working_source', 'fresh_working_value = 42;'),
                       ('index_source', 'staged_value = 7;')]:
        assert any(text in row['text'] for row in layout(name)['reading_rows']), (zoom, name, text)
    for name in ['historical_menu', 'dismissed', 'other_diff']:
        tab = active(name)
        assert tab['kind'] == 'review' and tab['path'] == 'a.cpp' and after in tab['revision'], (zoom, name, tab)
    assert active('preview')['preview'] and not active('kept')['preview'], zoom
    assert active('kept')['path'] == 'gone.cpp' and before in active('kept')['revision'], zoom
    assert active('historical_menu') == active('dismissed'), zoom
    focused = [n for n in layout('dismissed')['nodes'] if n['rendered'] and n['focused']]
    assert len(focused) == 1 and focused[0]['focus_target']['item'] == 'new.cpp', (zoom, focused)
    geometry[str(zoom)] = {}
    for name, path in [('working_revealed', 'untracked/fresh.cpp'), ('index_revealed', 'index.cpp')]:
        row = next(n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') == 'file_row' and n.get('focus_target', {}).get('item') == path)
        assert row['visible_rect']['height'] >= row['rect']['height'] - 1, (zoom, name, row)
        geometry[str(zoom)][name] = row['visible_rect']
    for name in ['historical_menu', 'working_menu']:
        nodes = [n for n in layout(name)['nodes'] if n['rendered'] and n.get('name', '').startswith('context_menu_item_')]
        labels = {n['name'].removeprefix('context_menu_item_') for n in nodes}
        assert {'Open diff', 'Open source', 'Keep open', 'Copy relative path'} <= labels, (zoom, name, labels)
        assert ('Reveal in tree' in labels) == (name == 'working_menu'), (zoom, name, labels)
        for node in nodes: assert node['visible_rect']['height'] >= node['rect']['height'] - 1, (zoom, name, node)
    assert (git('diff'), git('diff', '--cached'), git('status', '--porcelain')) == initial
    print(f'PASS {zoom}%: clicked-row menus preserve revisions, origin, preview/keep, and staged/unstaged scope', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=geometry), indent=2) + '\n')
