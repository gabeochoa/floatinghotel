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
paths = ['app/z.cpp', 'app/zz.cpp', 'long/deep/app.cpp', 'src/application.cpp',
         'left/common.cpp', 'right/common.cpp', '日本.cpp']
long_path = 'long/tree/' + 'deep_' * 30 + '/long_file.cpp'
paths += [long_path] + [f'files/f{i:03}.cpp' for i in range(120)]
for name in paths:
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('int value = 1;\n')
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Ranking fixture'),
             ('config', 'user.email', 'ranking@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Ranking fixture')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count):
        return f'wait_for_refresh\nwait_frames 12\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    def query(text):
        return 'click_ui file_picker_input\nkey CMD+A\n' + (f'type "{text}"\n' if text else 'key BACKSPACE\n') + 'wait_frames 8\n'
    def open_file(path):
        return 'key CMD+P\nwait_for_refresh\n' + query(path) + 'key ENTER\nwait_for_refresh\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += open_file('right/common.cpp') + open_file('left/common.cpp')
    script += 'key CMD+W\nwait_for_refresh\nkey CMD+P\nwait_for_refresh\n' + query('') + capture('recent_working', 2)
    script += query('APP') + capture('filename_rank', 2)
    script += query('common') + capture('duplicate_names', 2)
    script += query('app') + capture('deterministic_rank', 2)
    script += 'key ESCAPE\nclick_text "Ranking fixture"\nwait_for_refresh\nkey ENTER\nkey CMD+P\nwait_for_refresh\n'
    script += query('') + capture('historical_fresh', 3)
    script += query('left/common.cpp') + 'key ENTER\nwait_for_refresh\nkey CMD+P\nwait_for_refresh\n'
    script += query('') + capture('historical_recent', 4)
    script += 'click_ui file_picker_working_scope\n' + capture('working_scoped', 4)
    script += query('common') + 'resize 1100 800\n' + capture('narrow', 4)
    script += query('long_file') + capture('long_path', 4)
    script += 'key ESCAPE\nresize 1800 1100\nbench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def rows(name):
        return [n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') == 'file_picker_result']
    def labels(name):
        return [n['focus_target']['item'] for n in rows(name)]
    assert labels('recent_working')[:2] == ['right/common.cpp', 'left/common.cpp'], (zoom, labels('recent_working'))
    assert labels('filename_rank') == ['long/deep/app.cpp', 'src/application.cpp', 'app/z.cpp', 'app/zz.cpp'], (zoom, labels('filename_rank'))
    assert labels('filename_rank') == labels('deterministic_rank'), zoom
    assert labels('duplicate_names') == ['left/common.cpp', 'right/common.cpp'], zoom
    assert labels('historical_fresh')[0] == 'app/z.cpp', zoom
    assert labels('historical_recent')[0] == 'left/common.cpp', zoom
    assert labels('working_scoped')[:2] == ['right/common.cpp', 'left/common.cpp'], zoom
    for name in ['recent_working', 'historical_fresh', 'historical_recent', 'working_scoped']:
        assert len(rows(name)) < 20, (zoom, name, len(rows(name)))
    def luminance(color):
        values = [v / 255 for v in color[:3]]
        return sum(w * (v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4) for w, v in zip([.2126, .7152, .0722], values))
    for name in ['filename_rank', 'duplicate_names', 'narrow', 'long_path']:
        snapshot = layout(name)
        overlay = next(n for n in snapshot['nodes'] if n['rendered'] and n.get('name') == 'file_picker_overlay')['visible_rect']
        for row in rows(name):
            assert ''.join(span['text'] for span in row['text_spans']) == row['text'], (zoom, name, row)
            matches = ''.join(span['text'] for span in row['text_spans'] if span['color'] == [190, 215, 255, 255])
            assert matches.lower() == ('app' if name == 'filename_rank' else 'long_file' if name == 'long_path' else 'common'), (zoom, name, matches)
            assert row['text'].startswith(Path(row['focus_target']['item']).name), (zoom, name, row['text'])
            assert len(row['text_spans']) >= 2, (zoom, name, row)
            rect = row['visible_rect']
            assert rect['height'] >= row['rect']['height'] - 1, (zoom, name, rect)
            assert rect['x'] >= overlay['x'] and rect['x'] + rect['width'] <= overlay['x'] + overlay['width'] + 1, (zoom, name, rect)
            for span in row['text_spans']:
                a, b = sorted([luminance(span['color']), luminance(row['background'])])
                assert (b + .05) / (a + .05) >= 4.5, (zoom, name, span, row['background'])
    assert labels('long_path') == [long_path], zoom
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
    print(f'PASS {zoom}%: scoped recent files, closed files, filename priority, stable ties, highlights, duplicate paths, contrast, narrow geometry, virtualization', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
