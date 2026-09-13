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
large = ''.join(f'int {"NEEDLE" if i in [1, 4500, 6200] else "value"}_{i} = {i};\n' for i in range(1, 6201))
(repo / 'large.cpp').write_text(large)
(repo / 'cap.cpp').write_text(''.join(f'int CAP_{i} = {i};\n' for i in range(5001)))
(repo / 'encoded.txt').write_bytes(('ordinary\n' * 4999 + 'é UNICODE final').encode('utf-16'))
(repo / 'long.txt').write_text(' ' * (256 * 1024 - 3) + 'NEEDLE' + ' ' * 40 + 'é NEEDLE')
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Source Find fixture'),
             ('config', 'user.email', 'find@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Original source files')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
oid = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
subprocess.run(['git', '-C', str(repo), 'rm', 'large.cpp'], check=True, capture_output=True)
subprocess.run(['git', '-C', str(repo), 'commit', '-qm', 'Delete original source'], check=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count):
        return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    def source(path):
        return f'key CMD+P\nwait_frames 3\ntype "{path}"\nwait_for_refresh\nwait_frames 3\nkey ENTER\nwait_for_refresh\nwait_frames 3\nwait_for_refresh\n'
    script = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Original source files"\nwait_for_refresh\nkey ENTER\n'
    script += source('large.cpp') + 'key CMD+F\ntype "NEEDLE"\n' + capture('first', 3)
    script += 'key ENTER\n' + capture('later_page', 3)
    script += 'key ENTER\n' + capture('last_line', 3)
    script += 'key SHIFT+ENTER\n' + capture('previous', 3)
    script += 'click_ui diff_find_previous\n' + capture('first_again', 3)
    script += source('cap.cpp') + 'key CMD+F\ntype "CAP"\n' + capture('limited', 4)
    script += 'key ESCAPE\n' + source('encoded.txt') + 'key CMD+F\ntype "UNICODE"\n' + capture('encoded', 5)
    script += 'key ESCAPE\n' + source('long.txt') + 'key CMD+F\ntype "NEEDLE"\n' + capture('cross_boundary', 6)
    script += 'key ENTER\n' + capture('long_second', 6)
    script += 'key ESCAPE\nclick_ui content_document_5\nwait_for_refresh\nclick_ui content_document_6\n' + capture('long_return', 6)
    script += 'key CMD+F\n' + capture('long_reopened', 6)
    script += 'focus_ui diff_scroll\nkey CMD+HOME\n' + capture('full_page', 6)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=240'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=270)
    assert result.returncode == 0, (zoom, result.returncode, directory / 'run.log')
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def active(name):
        value = state(name)
        return next(t for t in value['tabs'] if t['id'] == value['active'])
    for name, line, column in [('first', 1, 5), ('later_page', 4500, 5), ('last_line', 6200, 5),
        ('previous', 4500, 5), ('first_again', 1, 5), ('encoded', 5000, 3),
        ('cross_boundary', 1, 256 * 1024 - 2), ('long_second', 1, 256 * 1024 + 46),
        ('long_reopened', 1, 256 * 1024 + 46)]:
        tab = active(name)
        assert tab['revision'] == oid, (zoom, name, tab)
        assert tab['find']['position']['line'] == line and tab['find']['position']['column'] == column, (zoom, name, tab)
        rows = [r for r in layout(name)['reading_rows'] if r['path'] == tab['path'] and r['line'] == line
                and r['column'] <= column <= r['column'] + len(r['text'])]
        assert rows, (zoom, name, 'Match was not rendered', tab)
        highlights = [n for n in layout(name)['nodes'] if n.get('name') == 'diff_find_match' and n['rendered'] and n['visible_rect']['height'] > 0]
        assert highlights, (zoom, name, 'Match was not visible')
        assert state(name)['source_find']['max_page_bytes'] <= 256 * 1024
        assert not state(name)['source_find']['error'], (zoom, name, state(name)['source_find'])
    for name in ['first', 'later_page', 'last_line', 'previous', 'first_again']:
        assert state(name)['source_find']['matches'] == 3
        assert state(name)['source_find']['scanned_bytes'] == len(large.encode())
    assert state('limited')['source_find']['matches'] == 5000 and state('limited')['source_find']['limited']
    assert any(n.get('name') == 'source_find_notice' and n.get('text') == 'Limited to the first 5,000 matches'
               and n['visible_rect']['height'] > 0 for n in layout('limited')['nodes'])
    assert state('cross_boundary')['source_find']['page_column'] > 1
    assert state('full_page')['source_find']['page_offset'] == 0 and state('full_page')['source_find']['page_column'] == 1
    assert state('limited')['source_find']['match_bytes'] <= 65536
    assert active('long_reopened')['find']['position'] == active('long_second')['find']['position']
    assert any(n['focused'] and n.get('focus_target', {}).get('control') == 'diff_find_input' for n in layout('later_page')['nodes'])
    print(f'PASS {zoom}%: complete historical source, page navigation, 5,000 limit, UTF-16, long-line columns and return', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
