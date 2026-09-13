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
for name in ['a.cpp', 'b.cpp']:
    (repo / name).write_text(''.join(f'int value_{i} = {i};\n' for i in range(20)))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Tree fixture'),
                ('config', 'user.email', 'tree@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Tree fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
for name in ['a.cpp', 'b.cpp']:
    p = repo / name
    p.write_text(p.read_text().replace('value_10 = 10', 'value_10 = 1000'))
working = {name: (repo / name).read_text() for name in ['a.cpp', 'b.cpp']}
initial_diff = subprocess.check_output(['git', '-C', str(repo), 'diff'])
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name, count=2):
    return f'wait_frames 8\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    for name, content in working.items():
        (repo / name).write_text(content)
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nmouse_move 1750 1050\n' + capture('idle', 1)
    script += 'hover_ui jump_to_diff:a.cpp\n' + capture('hover', 1)
    script += 'click_ui commit_file_filter\nkey TAB\n' + capture('focus', 1)
    script += 'hover_ui jump_to_diff:b.cpp\n' + capture('focus_other_hover', 1)
    script += 'click_ui jump_to_diff:a.cpp\nwait_frames 6\nmouse_move 1750 1050\n' + capture('selected', 1)
    script += 'click_ui commit_file_filter\nkey TAB\nhover_ui jump_to_diff:a.cpp\n' + capture('selected_focus_hover', 1)
    script += 'click_ui approve_file_btn\nwait_frames 4\nmouse_move 1750 1050\n' + capture('reviewed', 1)
    script += 'click_ui commit_file_filter\nkey TAB\nhover_ui jump_to_diff:b.cpp\n' + capture('reviewed_focus', 1)
    script += 'click_ui approve_file_btn\nwait_frames 4\nclick_ui comment_hunk_btn\nwait_frames 3\nclick_ui comment_input\ntype "Tree feedback"\nclick_ui comment_add_btn\nwait_frames 8\nkey ESCAPE\n' + capture('unresolved', 1)
    script += 'native_menu_action "Review Workspace (toggle)"\nclick_ui sidebar_working_files\nwait_frames 8\nclick_ui working_review_toggle\nwait_frames 6\nclick_text a.cpp\nwait_frames 6\nclick_text b.cpp\nwait_frames 6\nclick_text a.cpp\nwait_frames 6\n' + capture('legacy_seen', 1)
    script += 'touch_file b.cpp\nnative_menu_action "Ignore Whitespace (toggle)"\nwait_for_refresh\nnative_menu_action "Ignore Whitespace (toggle)"\nwait_for_refresh\n' + capture('legacy_changed', 1)
    script += 'native_menu_action "Review Workspace (toggle)"\nclick_ui sidebar_review\nwait_frames 6\n' + capture('changed', 1)
    script += 'click_text "Tree fixture"\nwait_for_refresh\nkey ENTER\nclick_ui approve_file_btn\nwait_frames 4\n' + capture('historical_reviewed', 2)
    script += 'click_text "Open file"\nwait_for_refresh\n' + capture('historical_source', 3)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'

    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())

    def row(name, path='a.cpp'):
        return next(n for n in layout(name)['nodes'] if n.get('focus_target', {}).get('region') == 'Tree' and n['focus_target']['item'] == path and n.get('name') in ['commit_changed_file', 'file_row'] and n['rendered'])

    def contents(name, path='a.cpp'):
        current = row(name, path)
        return [n for n in layout(name)['nodes'] if n['parent'] == current['id']]

    def status(name, path='a.cpp'):
        return next(n['name'].split(':', 1)[1] for n in contents(name, path) if n.get('name', '').startswith('tree_review_status:'))

    assert not row('idle')['hot'] and not row('idle')['visual_focus']
    assert row('hover')['hot'] and not row('hover')['visual_focus']
    assert row('focus')['focused'] and row('focus')['visual_focus'], (zoom, 'focus', row('focus'))
    assert row('focus_other_hover')['visual_focus'] and row('focus_other_hover', 'b.cpp')['hot']
    assert row('selected_focus_hover')['hot'] and row('selected_focus_hover')['visual_focus']
    assert row('selected')['background'] != row('idle')['background']
    assert row('selected_focus_hover')['background'] == row('selected')['background']
    for name in ['idle','hover','focus','selected','selected_focus_hover']:
        assert status(name) == 'unreviewed', (zoom, name, status(name))
    assert status('reviewed') == status('reviewed_focus') == 'reviewed', zoom
    assert status('unresolved') == 'unresolved', zoom
    assert status('changed', 'b.cpp') == status('legacy_changed', 'b.cpp') == 'changed', zoom
    assert status('historical_reviewed') == status('historical_source') == 'reviewed', zoom
    def stable_contents(name):
        current_row = row(name)['rect']
        return {n['name']: ({**n['rect'], 'x': round(n['rect']['x'] - current_row['x'], 3), 'y': round(n['rect']['y'] - current_row['y'], 3)}, n.get('text'), n.get('text_color')) for n in contents(name) if n.get('name') in ['jump_to_diff:a.cpp','file_name','tree_file_type','tree_additions','tree_deletions']}

    for group in [['idle','hover','focus','selected','selected_focus_hover','reviewed','reviewed_focus','unresolved'], ['legacy_seen','legacy_changed']]:
        original = stable_contents(group[0])
        for name in group[1:]:
            current = stable_contents(name)
            assert current == original, (zoom, name, original, current)
    assert subprocess.check_output(['git', '-C', str(repo), 'diff', '--cached']) == b''
    assert (repo / 'a.cpp').read_text() == working['a.cpp']
    assert (repo / 'b.cpp').read_text() == working['b.cpp'] + '# edited by e2e test\n'
    print(f'PASS {zoom}%: hover, selection, focus, review status, stable geometry, retained historical status', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
