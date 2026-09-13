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
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
(repo / 'a.cpp').write_text(''.join(('int LONG_COLUMN = ' + 'x' * 700 + ';\n') if i == 200 else f'int LINE_{i} = {i};\n' for i in range(1, 601)))
(repo / 'large.cpp').write_text(''.join(f'int LARGE_{i} = {i};\n' for i in range(1, 5001)))
(repo / 'gone.cpp').write_text(''.join(f'int REMOVED_{i} = {i};\n' for i in range(1, 101)))
(repo / 'literal:12').write_text('literal colon filename\n')
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Line fixture'),
             ('config', 'user.email', 'line@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Original lines')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
old = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
subprocess.run(['git', '-C', str(repo), 'rm', 'gone.cpp'], check=True, capture_output=True)
subprocess.run(['git', '-C', str(repo), 'commit', '-qm', 'Delete old file'], check=True)
(repo / 'a.cpp').write_text((repo / 'a.cpp').read_text().replace('LINE_80 = 80', 'LINE_80 = 8000'))
status_before = subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain'])
wrapper = out / 'bin'
wrapper.mkdir()
real_git = shutil.which('git')
(wrapper / 'git').write_text('#!/usr/bin/env python3\nimport os, sys, time\n'
    + "if 'cat-file' in sys.argv and 'blob' in sys.argv: time.sleep(0.5)\n"
    + f"os.execv({real_git!r}, [{real_git!r}, *sys.argv[1:]])\n")
(wrapper / 'git').chmod(0o755)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count):
        return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 12\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    def query(text, control='file_picker_input'):
        return f'click_ui {control}\nkey CMD+A\ntype "{text}"\nwait_frames 6\n'
    def open_file(text):
        return 'key CMD+P\nwait_for_refresh\n' + query(text) + 'key ENTER\nwait_for_refresh\n'
    def line(text):
        return 'key CTRL+G\nwait_frames 4\n' + query(text, 'line_picker_input') + 'key ENTER\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += open_file('literal:12') + capture('literal', 2)
    script += open_file('a.cpp:80:5') + capture('working_80', 3)
    script += line('500:9') + capture('working_500', 3)
    script += line('9999') + capture('beyond_eof', 3)
    script += query('0', 'line_picker_input') + 'key ENTER\n' + capture('invalid_line', 3)
    script += 'key ESCAPE\n' + capture('invalid_return', 3)
    script += line('600:999') + capture('last_line', 3)
    script += 'key ALT+LEFT\n' + capture('back_500', 3)
    script += line('200:300') + capture('wrapped_column', 3)
    script += open_file('large.cpp:4200:2') + capture('large_4200', 4)
    script += line('1:1') + capture('large_start', 4)
    script += 'key ALT+LEFT\n' + capture('large_back', 4)
    script += 'key CMD+P\nwait_for_refresh\n' + query('a.cpp:80:') + 'key ENTER\n' + capture('invalid_suffix', 4)
    script += 'key ESCAPE\nclick_text "Original lines"\nwait_for_refresh\nkey ENTER\nkey CMD+P\nwait_for_refresh\n'
    script += query('a.cpp:80:4') + 'key ENTER\nwait_frames 3\nscreenshot checking_position\n'
    script += query('a.cpp:90:3') + capture('superseded_position', 5)
    script += 'key ENTER\n' + capture('historical_90', 6)
    script += 'click_ui full_file_back\nwait_for_refresh\nclick_ui content_document_5\n' + capture('review_origin', 6) + line('400:7') + capture('review_400', 6)
    script += line('99999') + capture('outside_diff', 6)
    script += 'key ESCAPE\nkey ALT+LEFT\n' + capture('review_back', 6)
    script += 'click_text "Delete old file"\nwait_for_refresh\nkey ENTER\nclick_ui content_document_7\n' + line('90:4') + capture('deleted_90', 7)
    script += 'native_menu_action "Go to Line..."\nresize 1100 800\n' + capture('narrow_line_picker', 7)
    script += 'key ESCAPE\nresize 1800 1100\nbench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1', PATH=str(wrapper) + os.pathsep + os.environ['PATH']),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def active(name):
        snapshot = state(name)
        return next(t for t in snapshot['tabs'] if t['id'] == snapshot['active'])
    def anchor(name):
        snapshot = state(name)
        return snapshot['history'][snapshot['history_index']]['anchor']
    assert active('literal')['path'] == 'literal:12', zoom
    for name, path_name, row, column, revision in [('working_80', 'a.cpp', 80, 5, ''), ('working_500', 'a.cpp', 500, 9, ''),
            ('last_line', 'a.cpp', 600, 20, ''), ('back_500', 'a.cpp', 500, 9, ''),
            ('wrapped_column', 'a.cpp', 200, 300, ''), ('large_4200', 'large.cpp', 4200, 2, ''), ('large_start', 'large.cpp', 1, 1, ''),
            ('large_back', 'large.cpp', 4200, 2, ''), ('historical_90', 'a.cpp', 90, 3, old),
            ('review_400', 'a.cpp', 400, 7, old), ('deleted_90', 'gone.cpp', 90, 4, None)]:
        point = anchor(name)
        if name in ['review_400', 'deleted_90']:
            assert point['path'] == path_name and point['line'] == row and point['column'] == column, (zoom, name, point)
        else:
            snapshot = state(name)
            target = snapshot['history'][snapshot['history_index']]['location']['source']
            assert target['path'] == path_name and target['line'] == row and target['column'] == column, (zoom, name, target)
        snapshot = layout(name)
        viewport = next(n['visible_rect'] for n in snapshot['nodes'] if n['rendered'] and n.get('name') == ('diff_scroll' if name not in ['review_400', 'deleted_90'] else 'commit_detail_scroll'))
        assert any(r['path'] == path_name and r['line'] == row and r['rect']['y'] + r['rect']['height'] > viewport['y'] and r['rect']['y'] < viewport['y'] + viewport['height'] for r in snapshot['reading_rows']), (zoom, name)
        if name not in ['review_400', 'deleted_90']:
            assert active(name)['revision'] == revision and active(name)['kind'] == 'source', (zoom, name, active(name))
    assert anchor('deleted_90')['side'] == 'before', zoom
    wrapped = [r for r in layout('wrapped_column')['reading_rows'] if r['line'] == 200 and r['offset'] <= 299 < r['offset'] + len(r['text'])]
    assert wrapped and wrapped[0]['rect']['height'] > 0, (zoom, wrapped)
    viewport = next(n['visible_rect'] for n in layout('wrapped_column')['nodes'] if n['rendered'] and n.get('name') == 'diff_scroll')
    assert any(viewport['y'] <= r['rect']['y'] and r['rect']['y'] + r['rect']['height'] <= viewport['y'] + viewport['height'] for r in wrapped), (zoom, wrapped, viewport)
    for name in ['working_80', 'working_500', 'historical_90', 'review_400']:
        focused = [n for n in layout(name)['nodes'] if n['rendered'] and n['focused']]
        assert len(focused) == 1 and focused[0].get('focus_target', {}).get('region') in ['Code', 'DocumentTabs'], (zoom, name, focused)
    for invalid, previous in [('beyond_eof', 'working_500'), ('invalid_line', 'working_500'), ('invalid_return', 'working_500'),
            ('invalid_suffix', 'large_back'), ('outside_diff', 'review_400')]:
        a, b = state(invalid), state(previous)
        assert a['active'] == b['active'] and a['history_index'] == b['history_index'] and a['tabs'] == b['tabs'], (zoom, invalid)
    assert active('superseded_position')['kind'] == 'review', zoom
    assert anchor('review_back') == anchor('review_origin'), (zoom, anchor('review_back'), anchor('review_origin'))
    assert active('review_400')['id'] == 5 and active('deleted_90')['kind'] == 'review', zoom
    for name, message in [('beyond_eof', 'beyond the end'), ('invalid_line', 'positive line'), ('outside_diff', 'not in this diff')]:
        assert any(message in n.get('text', '') for n in layout(name)['nodes'] if n['rendered']), (zoom, name)
    for name in ['beyond_eof', 'narrow_line_picker']:
        snapshot = layout(name)
        for control in ['line_picker_input', 'line_picker_go', 'line_picker_path', 'line_picker_status']:
            node = next(n for n in snapshot['nodes'] if n['rendered'] and n.get('name') == control)
            assert node['visible_rect']['height'] >= node['rect']['height'] - 1, (zoom, name, node)
            assert node['rect']['x'] >= 0 and node['rect']['x'] + node['rect']['width'] <= snapshot['viewport']['width'] + 1, (zoom, name, node)
    assert any(n.get('name') == 'line_picker_status' and 'Line[:column]' in n.get('text', '') and n.get('text_color') == [228, 230, 235, 255] for n in layout('narrow_line_picker')['nodes'] if n['rendered']), zoom
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == status_before
    print(f'PASS {zoom}%: literal colon paths, precise source/review lines, column bounds, EOF errors retain reader, later pages, historical scope, stale reads, deleted side, narrow geometry', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, historical_revision=old), indent=2) + '\n')
