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
(repo / 'a.cpp').write_text(''.join(f'int ALPHA_{i:03d} = {i};\n' for i in range(1, 151)))
(repo / 'b.cpp').write_text(''.join(f'int BETA_{i:03d} = {i};\n' for i in range(1, 71)))
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Find fixture'),
             ('config', 'user.email', 'find@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Find fixture')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count=3):
        return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    setup = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    setup += 'click_text "Find fixture"\nwait_for_refresh\nkey ENTER\n' + capture('review_before', 2)
    script = setup + 'key CMD+F\n' + capture('review_open', 2)
    script += 'type "ALPHA"\n' + capture('review_found', 2)
    script += 'key ENTER\n' + capture('review_second', 2)
    script += 'key SHIFT+ENTER\n' + capture('review_previous', 2)
    script += 'key ESCAPE\n' + capture('review_closed', 2)
    script += 'key CMD+P\nwait_frames 3\ntype "a.cpp:50"\nwait_for_refresh\nwait_frames 3\nkey ENTER\n' + capture('source_before')
    script += 'key CMD+F\n' + capture('source_open')
    script += 'type "ALPHA"\n' + capture('source_found')
    script += 'key ENTER\nkey ENTER\n' + capture('source_third')
    script += 'key CMD+P\nwait_frames 3\ntype "b.cpp"\nwait_for_refresh\nwait_frames 3\nkey ENTER\n' + capture('other_before', 4)
    script += 'key CMD+F\ntype "BETA"\nkey ENTER\n' + capture('other_second', 4)
    script += 'click_ui content_document_3\n' + capture('source_return', 4)
    script += 'click_ui diff_find_close\n' + capture('source_closed', 4)
    script += 'key CMD+F\n' + capture('source_reopened', 4)
    script += 'key ESCAPE\nresize 1150 850\nwait_frames 15\n' + capture('narrow_before', 4)
    script += 'key CMD+F\n' + capture('narrow_open', 4)
    script += 'key ESCAPE\n' + capture('narrow_closed', 4)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    def replay(name, commands):
        path = directory / (name + '.e2e')
        path.write_text(commands)
        with (directory / (name + '.log')).open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
                f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
                env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
        assert result.returncode == 0, (zoom, result.returncode, directory / (name + '.log'))
    replay('journey', script)
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def active(name):
        value = state(name)
        return next(t for t in value['tabs'] if t['id'] == value['active'])
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def node(name, debug):
        return next(n for n in layout(name)['nodes'] if n.get('name') == debug and n['rendered'])
    def focus(name):
        return next(n['focus_target'] for n in layout(name)['nodes'] if n['focused'] and 'focus_target' in n)
    for before, opened, closed, viewport in [('review_before', 'review_open', None, 'commit_detail_scroll'),
        ('source_before', 'source_open', None, 'diff_scroll'),
        ('narrow_before', 'narrow_open', 'narrow_closed', 'diff_scroll')]:
        expected = node(before, viewport)
        for name in [opened] + ([closed] if closed else []):
            actual = node(name, viewport)
            assert actual['rect'] == expected['rect'], (zoom, before, name, 'Find resized viewport')
            assert actual.get('scroll') == expected.get('scroll'), (zoom, before, name, 'Find moved scroll')
            rows = [(r['path'], r['line'], r['column'], r['rect']) for r in layout(name)['reading_rows']]
            prior = [(r['path'], r['line'], r['column'], r['rect']) for r in layout(before)['reading_rows']]
            assert rows == prior, (zoom, before, name, 'Find moved code')
        assert focus(opened)['region'] == 'Find'
        bar = node(opened, 'diff_find_bar')['rect']
        main = node(opened, 'main_content')['rect']
        assert bar['x'] >= main['x'] and bar['x'] + bar['width'] <= main['x'] + main['width'] + 1
        assert abs(bar['height'] - 34 * zoom / 100) < 1
    first_source_line = min(r['line'] for r in layout('source_before')['reading_rows'])
    for name, index, line in [('review_found', 0, 1), ('review_second', 1, 2), ('review_previous', 0, 1),
        ('source_found', 0, first_source_line), ('source_third', 2, first_source_line + 2), ('source_return', None, first_source_line + 2), ('other_second', 1, 2)]:
        find = active(name)['find']
        assert (index is None or find['index'] == index) and find['position']['line'] == line, (zoom, name, find)
        assert any(r['line'] == line for r in layout(name)['reading_rows']), (zoom, name, 'Match not rendered')
    for name in ['source_return', 'source_reopened']:
        assert any(n.get('text') == 'ALPHA' and n.get('focus_target', {}).get('control') == 'diff_find_input'
                   for n in layout(name)['nodes']), (zoom, name, 'Restored query is not displayed')
        for field in ['query', 'position', 'open']:
            assert active(name)['find'][field] == active('source_third')['find'][field]
    for name in ['review_closed', 'source_closed', 'narrow_closed']:
        assert focus(name)['region'] == 'Code' and focus(name)['item'] == 'viewport', (zoom, name, focus(name))
        assert not active(name)['find']['open']
    for name in ['source_open', 'other_before']:
        assert active(name)['find']['query'] == ''
    record = next(r for r in layout('review_before')['reading_rows'] if r['path'] == 'a.cpp' and r['line'] == 1 and r['sign'] == '+')
    text_node = next(n for n in layout('review_before')['nodes'] if n['id'] == record['id'])
    glyph = text_node['measured_text_width'] / len(text_node['text'])
    x, y = record['content_x'] + 4 * glyph, record['rect']['y'] + record['rect']['height'] / 2
    replay('selection', setup + f'drag_to {x} {y} {x+5*glyph} {y}\n' + capture('selected', 2)
        + 'key CMD+F\n' + capture('seeded', 2) + 'key ESCAPE\n' + capture('seed_closed', 2))
    assert layout('selected')['selection_text'] == 'ALPHA'
    assert active('seeded')['find']['query'] == 'ALPHA'
    assert active('seeded')['find']['position']['column'] == 5
    assert layout('selected')['reading_rows'] == layout('seeded')['reading_rows']
    assert focus('seed_closed')['region'] == 'Code'
    print(f'PASS {zoom}%: per-document Find, seed, match positions, keyboard traversal, code focus and unchanged normal/narrow viewport', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
