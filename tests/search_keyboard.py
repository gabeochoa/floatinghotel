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
(repo / 'a.cpp').write_text(''.join(f'int NEEDLE_{i:02d} = {i};\n' for i in range(1, 81))
    + ''.join(f'int filler_{i} = {i};\n' for i in range(81, 5000)) + 'int NEEDLE_LATE = 5000;\n')
(repo / 'gone.cpp').write_text('int NEEDLE_OLD_1 = 1;\nint NEEDLE_OLD_2 = 2;\n')
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Keyboard search fixture'),
             ('config', 'user.email', 'keyboard@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Original keyboard files')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
original = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
subprocess.run(['git', '-C', str(repo), 'rm', 'gone.cpp'], check=True, capture_output=True)
subprocess.run(['git', '-C', str(repo), 'commit', '-qm', 'Delete historical source'], check=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count=3):
        return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Original keyboard files"\nwait_for_refresh\nkey ENTER\nkey CMD+SHIFT+F\nwait_frames 3\nclick_ui repo_search_input\ntype "NEEDLE"\n' + capture('results', 2)
    script += 'key DOWN\n' + capture('first')
    script += 'key DOWN\n' * 19 + capture('twentieth')
    script += 'key CMD+DOWN\n' + capture('modified')
    script += 'key ENTER\n' + capture('kept')
    script += 'key ESCAPE\n' + capture('search_focus')
    script += 'hover_ui repo_search_results\nscroll_wheel 0 100\nwait_frames 30\nclick_ui repo_search_file\nclick_ui repo_search_input\nkey DOWN\n' + capture('deleted', 4)
    script += 'key DOWN\n' + capture('deleted_second', 4)
    script += 'key ENTER\n' + capture('deleted_kept', 4)
    script += 'key ESCAPE\n' + capture('deleted_focus', 4)
    script += 'click_ui repo_search_input\nkey CMD+A\ntype "NEEDLE_LATE"\nkey ENTER\n' + capture('new_query', 4)
    script += 'key DOWN\n' + capture('later_line', 4)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, (zoom, result.returncode, directory / 'run.log')
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def active(name):
        value = state(name)
        return next(t for t in value['tabs'] if t['id'] == value['active'])
    def source(name):
        value = state(name)
        return value['history'][value['history_index']]['location']['source']
    def nodes(name):
        return json.loads((directory / f'{name}.json').read_text())['nodes']
    assert state('results')['search']['matches'] == 83
    for name, path, line in [('first', 'a.cpp', 1), ('twentieth', 'a.cpp', 20), ('modified', 'a.cpp', 20),
                             ('deleted', 'gone.cpp', 1), ('deleted_second', 'gone.cpp', 2), ('later_line', 'a.cpp', 5000)]:
        assert active(name)['path'] == path and active(name)['revision'] == original, (zoom, name, active(name))
        assert source(name)['line'] == line, (zoom, name, source(name))
        layout = json.loads((directory / (name + '.json')).read_text())
        assert any(row['path'] == path and row['line'] == line for row in layout['reading_rows']), (zoom, name, 'Destination line not rendered')
        assert any(n.get('focus_target', {}).get('control') == 'repo_search_input' and n['focused'] for n in nodes(name)), (zoom, name, 'Search focus lost')
        selected = state(name)['search']['selected']
        assert selected is not None and state(name)['search']['open']
    assert active('first')['preview'] and not active('kept')['preview']
    assert active('deleted')['preview'] and not active('deleted_kept')['preview']
    for kept, returned in [('kept', 'search_focus'), ('deleted_kept', 'deleted_focus')]:
        assert active(kept) == active(returned)
        assert any(n.get('focus_target', {}).get('region') == 'DocumentTabs' and n['focused'] for n in nodes(kept))
        assert any(n.get('focus_target', {}).get('control') == 'repo_search_input' and n['focused'] for n in nodes(returned))
        assert state(returned)['search']['open']
    assert state('deleted')['search']['groups'][0]['collapsed']
    assert state('new_query')['search']['matches'] == 1 and state('new_query')['search']['submissions'] == 2
    for name in ['twentieth', 'deleted_second', 'later_line']:
        rendered = nodes(name)
        expected = str(source(name)['line']) + '  '
        row = next(n for n in rendered if n.get('name') == 'repo_search_result' and n['text'].startswith(expected))
        assert row['visible_rect']['height'] >= row['rect']['height'] - 1, (zoom, name, row)
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
    print(f'PASS {zoom}%: arrows preview exact historical lines, retain input focus, reveal results, skip collapsed groups, Enter keeps, Escape returns, deleted sources', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
