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
paths = [f'a/f{i:03}.cpp' for i in range(40)] + ['z/deep/target.cpp']
for name in paths:
    p = repo / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(''.join(f'int value_{i} = {i};\n' for i in range(300 if 'target' in name else 1)))
def git(*args): return subprocess.check_output(['git', '-C', str(repo), *args])
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Reveal fixture'),
                ('config', 'user.email', 'reveal@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Historical tree')]: git(*command)
historical = git('rev-parse', 'HEAD').decode().strip()
(repo / 'current/deep').mkdir(parents=True)
git('mv', 'z/deep/target.cpp', 'current/deep/target.cpp')
git('commit', '-qm', 'Rename target')
for p in repo.glob('a/*.cpp'): p.write_text('int changed = 1;\n')
p = repo / 'current/deep/target.cpp'
p.write_text(p.read_text().replace('value_0 = 0', 'value_0 = 1000'))
initial_diff = git('diff')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
geometry = {}
for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count): return f'wait_frames 12\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    def picker(path): return f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nkey ENTER\nwait_for_refresh\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Historical tree"\nwait_for_refresh\nkey ENTER\nclick_ui commit_file_filter\nkey TAB\nworkspace_checkpoint 2 tree_entry\nscreenshot tree_entry\ntype "z"\nwait_frames 6\nworkspace_checkpoint 2 tree_z\nscreenshot tree_z\nkey RIGHT\nwait_frames 6\nworkspace_checkpoint 2 tree_deep\nscreenshot tree_deep\nkey RIGHT\nwait_frames 8\nworkspace_checkpoint 2 tree_target\nscreenshot tree_target\nclick_text "Open file"\nwait_for_refresh\nkey ENTER\n'
    script += capture('historical_source', 3)
    script += 'click_ui content_document_2\nwait_for_refresh\nclick_ui commit_file_filter\nkey TAB\nkey LEFT\nkey LEFT\nkey LEFT\nkey LEFT\n' + capture('collapsed', 3)
    script += picker('a/f000.cpp') + 'click_ui content_document_3\nwait_for_refresh\n' + capture('historical_reveal', 4)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -20\n' + capture('historical_reading', 4)
    script += 'native_menu_action "Review Workspace (toggle)"\nnative_menu_action "Tree View"\nclick_ui sidebar_working_files\nwait_frames 8\nclick_ui tree_directory:a/\nwait_frames 4\nclick_ui tree_directory:current/deep/\nclick_ui tree_directory:a/\n' + capture('files_historical', 4)
    script += picker('current/deep/target.cpp') + capture('working_reveal', 5)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -20\n' + capture('working_reading', 5)
    script += 'hover_ui sidebar_files\nscroll_wheel 0 200\n' + capture('away', 5)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -20\n' + capture('away_reading', 5)
    script += 'click_ui open_tabs_menu\nwait_frames 3\nclick_ui "context_menu_item_Historical tree"\nwait_for_refresh\n' + capture('files_history', 5)
    script += 'click_ui sidebar_review\nwait_frames 8\n' + capture('back_review', 5)
    script += 'hover_ui commit_files_scroll\nscroll_wheel 0 200\nwait_frames 6\nclick_ui commit_file_filter\ntype "definitely_absent"\n' + capture('empty_filter', 5)
    script += 'key CMD+A\nkey BACKSPACE\n' + capture('cleared_filter', 5)
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
    def rows(name, path): return [n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') in ['file_row', 'commit_changed_file'] and n.get('focus_target', {}).get('item') == path]
    def scroll(name, control): return next(n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') == control)
    def revealed(name, path):
        matching = rows(name, path)
        assert len(matching) == 1, (zoom, name, matching)
        row = matching[0]
        assert row['visible_rect']['height'] >= row['rect']['height'] - 1, (zoom, name, row)
        assert not row['focused'], (zoom, name, 'reveal stole focus')
        return row['visible_rect']
    geometry[str(zoom)] = {}
    for name, path in [('historical_reveal', 'z/deep/target.cpp'), ('working_reveal', 'current/deep/target.cpp'), ('back_review', 'z/deep/target.cpp')]:
        geometry[str(zoom)][name] = revealed(name, path)
    assert not rows('collapsed', 'z/deep/target.cpp'), zoom
    empty = scroll('empty_filter', 'commit_files_scroll')
    assert empty['scroll']['content_height'] <= empty['rect']['height'], (zoom, 'phantom empty scroll', empty)
    assert scroll('cleared_filter', 'commit_files_scroll')['scroll']['y'] == 0, zoom
    for before, after, control in [('historical_reveal', 'historical_reading', 'commit_files_scroll'), ('working_reveal', 'working_reading', 'sidebar_files'), ('away', 'away_reading', 'sidebar_files')]:
        assert scroll(before, control)['scroll'] == scroll(after, control)['scroll'], (zoom, before, after)
        assert state(before)['history_index'] == state(after)['history_index'], (zoom, before, after)
    assert not any(n['visible_rect']['height'] > 0 for n in rows('away_reading', 'current/deep/target.cpp')), zoom
    for name in ['files_historical', 'files_history']:
        assert not rows(name, 'z/deep/target.cpp'), (zoom, name)
    for name in ['historical_source', 'historical_reveal']:
        current = state(name)
        tab = next(t for t in current['tabs'] if t['id'] == current['active'])
        assert tab['revision'] == historical and tab['path'] == 'z/deep/target.cpp', (zoom, name, tab)
    print(f'PASS {zoom}%: explicit navigation reveals files once; reading preserves tree position and revision scope', flush=True)
assert git('diff', '--cached') == b''
assert git('diff') == initial_diff
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=geometry), indent=2) + '\n')
