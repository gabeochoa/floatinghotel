import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
for directory in ['src', 'tests']:
    folder = repo / directory
    folder.mkdir()
    (folder / 'a.cpp').write_text(''.join(f'int NEEDLE_{i} = {i};\n' for i in range(1, 3001)))
(repo / 'long.cpp').write_text('x' * 900 + ' needle 日本 needle\n')
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Grouped search fixture'),
             ('config', 'user.email', 'groups@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Grouped search files')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count=1):
        return f'wait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'key CMD+SHIFT+F\nwait_frames 3\nclick_ui repo_search_input\ntype "NEEDLE"\n' + capture('grouped') + 'bench_frames 120\nexpect_p99_below 20\n'
    script += 'click_ui repo_search_file\n' + capture('collapsed')
    script += 'click_ui repo_search_result\n' + capture('source', 2)
    script += 'click_ui repo_search_close\nkey CMD+SHIFT+F\n' + capture('retained', 2)
    script += 'click_ui repo_search_file\n' + capture('expanded', 2)
    script += 'click_ui repo_search_input\nkey CMD+A\ntype "needle"\n' + capture('long_match', 2)
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
    def search(name):
        return state(name)['search']
    def nodes(name):
        return json.loads((directory / f'{name}.json').read_text())['nodes']
    assert search('grouped')['matches'] == 5000 and search('grouped')['truncated']
    assert [(g['file'], g['matches']) for g in search('grouped')['groups']] == [('src/a.cpp', 3000), ('tests/a.cpp', 2000)]
    assert search('grouped')['visible_rows'] == 5002
    assert search('collapsed')['visible_rows'] == 2002 and search('collapsed')['groups'][0]['collapsed']
    source = state('source')
    assert next(t for t in source['tabs'] if t['id'] == source['active'])['path'] == 'tests/a.cpp'
    assert search('retained')['groups'] == search('source')['groups']
    assert search('retained')['submissions'] == 1
    assert search('expanded')['visible_rows'] == 5002
    assert search('grouped')['highlight_bytes'] == 320000
    assert search('grouped')['row_metadata_bytes'] < 256 * 1024
    for name in ['grouped', 'collapsed', 'source', 'retained', 'expanded', 'long_match']:
        rendered = nodes(name)
        assert sum(n.get('name') in ['repo_search_result', 'repo_search_file'] for n in rendered) < 60
        for n in rendered:
            if n.get('name') in ['repo_search_result', 'repo_search_file']:
                assert n['rect']['width'] > 0 and n['rect']['height'] > 0
    headers = [n for n in nodes('collapsed') if n.get('name') == 'repo_search_file_label']
    assert len(headers) == 2 and any('src' in n['text'] for n in headers) and any('tests' in n['text'] for n in headers)
    icons = [n for n in nodes('collapsed') if n.get('name') == 'repo_search_file_chevron']
    assert len(icons) == 2
    for icon, label in zip(sorted(icons, key=lambda n: n['rect']['y']), sorted(headers, key=lambda n: n['rect']['y'])):
        assert icon['rect']['x'] + icon['rect']['width'] <= label['rect']['x'] + .5
        assert abs(icon['rect']['y'] + icon['rect']['height'] / 2 - label['rect']['y'] - label['rect']['height'] / 2) < 1
    for name, word in [('grouped', 'NEEDLE'), ('long_match', 'needle')]:
        result = next(n for n in nodes(name) if n.get('name') == 'repo_search_result')
        highlights = [span for span in result['text_spans'] if span['color'][:3] == [190, 215, 255]]
        assert highlights and all(word in span['text'] for span in highlights), (zoom, name, result)
        rect = result['visible_rect']
        pixels = Image.open(directory / (name + '.png')).convert('RGB').crop((int(rect['x']), int(rect['y']),
            int(rect['x'] + rect['width']), int(rect['y'] + rect['height'])))
        assert any(b - r > 30 and g - r > 15 and r > 80 for r, g, b in pixels.get_flattened_data()), (zoom, name, 'No visible highlight pixels')
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
    print(f'PASS {zoom}%: 5000 matches grouped by full path/revision, collapse and retained state, virtual rows, source navigation, highlighted long-line excerpt', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
