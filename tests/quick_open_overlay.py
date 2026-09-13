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
(repo / 'files').mkdir(parents=True)
for i in range(70):
    content = ''.join(f'int value_{line} = {line};\n' for line in range(400)) if i == 45 else f'int file_{i} = {i};\n'
    (repo / f'files/f{i:03}.cpp').write_text(content)
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Picker overlay'),
             ('config', 'user.email', 'picker@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Picker review')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
geometry = {}

def same_visits(a, b):
    if len(a) != len(b):
        return False
    for original, current in zip(a, b):
        original, current = dict(original), dict(current)
        if 'anchor' in original and 'anchor' in current:
            left, right = dict(original.pop('anchor')), dict(current.pop('anchor'))
            if abs(left.pop('fraction') - right.pop('fraction')) > 0.00001 or left != right:
                return False
        if original != current:
            return False
    return True
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()

    def capture(name, count=2):
        return f'wait_for_refresh\nwait_frames 12\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'

    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Picker review"\nwait_for_refresh\nkey ENTER\nclick_ui commit_file_filter\ntype "f045"\nwait_frames 8\nexpect_text "f045.cpp"\nscreenshot filtered_target\nclick_ui jump_to_diff:files/f045.cpp\nwait_frames 8\nhover_ui commit_detail_scroll\nscroll_wheel 0 -20\nwait_frames 12\nclick_ui content_document_2\n' + capture('review_before')
    script += 'key CMD+P\n' + capture('review_open')
    script += 'type "files/f"\nwait_for_refresh\n' + 'key DOWN\n' * 45 + capture('chosen')
    script += 'hover_ui commit_detail_scroll\nscroll_wheel 0 -40\n' + capture('review_blocked')
    script += 'key ESCAPE\n' + capture('review_return')
    outside = 'click 700 60\n' if zoom == 200 else 'click_ui content_document_1\n'
    script += 'key CMD+P\nwait_frames 8\n' + outside + capture('outside_return')
    script += 'key CMD+P\nwait_frames 8\nwait_for_refresh\nkey ENTER\nwait_for_refresh\n' + capture('source_opened', 3)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -20\nwait_frames 12\nclick_ui content_document_3\n' + capture('source_before', 3)
    script += 'key CMD+P\n' + capture('source_picker', 3)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -40\n' + capture('source_blocked', 3)
    script += 'key ESCAPE\n' + capture('source_return', 3)
    script += 'resize 1100 800\nwait_frames 24\n' + capture('narrow_before', 3)
    script += 'key CMD+P\n' + capture('narrow_picker', 3)
    script += 'key ESCAPE\n' + capture('narrow_return', 3)
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

    def node(name, control):
        return next(n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') == control)

    def focused(name):
        return [n for n in layout(name)['nodes'] if n['focused'] and n['rendered']]

    geometry[str(zoom)] = {}
    for before, opened, blocked, returned, control, tab in [
        ('review_before', 'review_open', 'review_blocked', 'review_return', 'commit_detail_scroll', 2),
        ('source_before', 'source_picker', 'source_blocked', 'source_return', 'diff_scroll', 3),
        ('narrow_before', 'narrow_picker', 'narrow_picker', 'narrow_return', 'diff_scroll', 3)]:
        original = node(before, control)
        for name in [opened, blocked, returned]:
            current = node(name, control)
            assert original['rect'] == current['rect'] and original['scroll'] == current['scroll'], (zoom, before, name, original, current)
            assert state(name)['active'] == state(before)['active'] and same_visits(state(name)['history'], state(before)['history']), (zoom, before, name)
            assert layout(name)['selection_text'] == layout(before)['selection_text'], (zoom, before, name)
            assert layout(name)['reading_rows'] == layout(before)['reading_rows'], (zoom, before, name)
        focus = focused(returned)
        assert len(focus) == 1 and focus[0]['focus_target']['control'] == f'content_document_{tab}', (zoom, returned, focus)
        overlay = node(opened, 'file_picker_overlay')
        assert overlay['visible_rect']['width'] >= overlay['rect']['width'] - 1 and overlay['visible_rect']['height'] >= overlay['rect']['height'] - 1, (zoom, opened, overlay)
        assert overlay['rect']['width'] < (1100 if before == 'narrow_before' else 1800) - 20, (zoom, opened, overlay)
        assert focused(opened)[0]['focus_target']['region'] == 'Picker', (zoom, opened)
        rows = [n for n in layout(opened)['nodes'] if n['rendered'] and n.get('name') == 'file_picker_result']
        selected_rows = [n for n in rows if n.get('background') == [62, 82, 111, 255]]
        assert rows and rows[0]['font_size']['value'] == 14, (zoom, opened, rows)
        assert len(selected_rows) == 1, (zoom, opened, selected_rows)
        for selected_row in selected_rows:
            assert selected_row['visible_rect']['height'] >= selected_row['rect']['height'] - 1, (zoom, opened, selected_row)

        geometry[str(zoom)][before] = {'viewport': original['rect'], 'overlay': overlay['rect'], 'scroll': original['scroll']}
    selected = next(n for n in layout('chosen')['nodes'] if n['rendered'] and n.get('name') == 'file_picker_result' and n.get('focus_target', {}).get('item') == 'files/f045.cpp')
    assert selected['visible_rect']['height'] >= selected['rect']['height'] - 1, (zoom, selected)
    assert len([n for n in layout('chosen')['nodes'] if n['rendered'] and n.get('name') == 'file_picker_result']) < 40, zoom
    assert state('outside_return')['active'] == 2 and same_visits(state('outside_return')['history'], state('review_before')['history']), zoom
    assert not any(n['rendered'] and n.get('name') == 'file_picker_overlay' for n in layout('outside_return')['nodes']), zoom
    source = next(t for t in state('source_opened')['tabs'] if t['id'] == 3)
    assert source['kind'] == 'source' and source['path'] == 'files/f045.cpp' and not source['preview'], (zoom, source)
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
    print(f'PASS {zoom}%: picker overlay, unchanged reader, blocked background scroll/click, focus return, narrow window', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=geometry), indent=2) + '\n')
