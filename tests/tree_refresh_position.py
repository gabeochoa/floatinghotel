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
(repo / 'files').mkdir(parents=True)
paths = [f'files/f{i:03}.cpp' for i in range(80)]
for name in paths + ['files/a_new.cpp', 'files/b_new.cpp']:
    (repo / name).write_text(''.join(f'int value_{i} = {i};\n' for i in range(80)))
def git(*args): return subprocess.check_output(['git', '-C', str(repo), *args])
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Tree position fixture'),
                ('config', 'user.email', 'position@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Tree positions')]: git(*command)
for name in paths:
    p = repo / name
    p.write_text(p.read_text().replace('value_40 = 40', 'value_40 = 4000'))
reserved = (repo / 'files/a_new.cpp').read_text()
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
geometry = {}
for zoom in [100, 140, 200]:
    for name in ['files/a_new.cpp', 'files/b_new.cpp']: (repo / name).write_text(reserved)
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name): return f'wait_frames 16\nworkspace_checkpoint 1 {name}\nscreenshot {name}\n'
    def query(text): return 'click_ui commit_file_filter\nkey CMD+A\nkey BACKSPACE\n' + (f'type "{text}"\n' if text else '') + 'wait_frames 8\n'
    refresh = 'native_menu_action "Ignore Whitespace (toggle)"\nwait_for_refresh\nnative_menu_action "Ignore Whitespace (toggle)"\nwait_for_refresh\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui commit_file_filter\nkey TAB\nkey RIGHT\n'
    script += 'key DOWN\n' * 40 + capture('review_before')
    script += 'touch_file files/a_new.cpp\n' + refresh + capture('review_after')
    script += query('f04') + capture('filtered_group')
    script += query('f041') + 'key TAB\n' + capture('filtered_neighbor')
    script += 'key ENTER\n' + capture('filtered_kept')
    script += query('no_matching_file') + capture('empty')
    script += query('') + capture('restored')
    script += 'native_menu_action "Review Workspace (toggle)"\nright_click_ui jump_to_diff:files/f041.cpp\nwait_frames 4\nclick_ui "context_menu_item_Reveal in tree"\nwait_frames 8\nclick_ui file_row\ntype "f041"\n' + capture('files_before')
    script += 'touch_file files/b_new.cpp\n' + refresh + capture('files_after')
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -10\n' + capture('reading')
    script += 'hover_ui sidebar_files\nscroll_wheel 0 200\n' + capture('manual_top')
    script += refresh + capture('manual_refreshed')
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
    def row(name, path): return next(n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') in ['file_row', 'commit_changed_file'] and n.get('focus_target', {}).get('item') == path)
    def scroll(name, control): return next(n['scroll'] for n in layout(name)['nodes'] if n['rendered'] and n.get('name') == control)
    geometry[str(zoom)] = {}
    for before, after, path, control in [('review_before', 'review_after', 'files/f040.cpp', 'commit_files_scroll'), ('files_before', 'files_after', 'files/f041.cpp', 'sidebar_files')]:
        first, second = row(before, path), row(after, path)
        assert first['focused'] and second['focused'], (zoom, before, first, second)
        assert abs(first['rect']['y'] - second['rect']['y']) < 1, (zoom, before, first['rect'], second['rect'])
        assert abs(scroll(after, control)['y'] - scroll(before, control)['y'] - 28 * zoom / 100) < 1, (zoom, before, scroll(before, control), scroll(after, control))
        assert state(before)['history_index'] == state(after)['history_index'], (zoom, before)
        geometry[str(zoom)][before] = {'before': first['rect'], 'after': second['rect']}
    assert row('filtered_neighbor', 'files/f041.cpp')['focused'], zoom
    assert not any(n['rendered'] and n.get('name') == 'commit_changed_file' for n in layout('empty')['nodes']), zoom
    assert row('restored', 'files/f041.cpp')['visible_rect']['height'] > 0, zoom
    focused = [n for n in layout('restored')['nodes'] if n['focused'] and n['rendered']]
    assert len(focused) == 1 and focused[0]['focus_target']['control'] == 'commit_file_filter', (zoom, focused)
    assert scroll('files_after', 'sidebar_files') == scroll('reading', 'sidebar_files'), zoom
    assert scroll('manual_top', 'sidebar_files')['y'] == scroll('manual_refreshed', 'sidebar_files')['y'] == 0, zoom
    assert state('manual_top')['history_index'] == state('manual_refreshed')['history_index'], zoom
    assert git('diff', '--cached') == b''
    for name in ['files/a_new.cpp', 'files/b_new.cpp']: assert (repo / name).read_text() == reserved + '# edited by e2e test\n'
    for name in paths: assert (repo / name).read_text() == reserved.replace('value_40 = 40', 'value_40 = 4000')
    print(f'PASS {zoom}%: refresh preserves row coordinates and focus; filtering restores a surviving path', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=geometry), indent=2) + '\n')
