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
(repo / 'large.cpp').write_text(''.join(f'int café_{i} = {i};\n' for i in range(8000)))
(repo / 'other.cpp').write_text('int other = 0;\n')
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Page fixture'),
                ('config', 'user.email', 'page@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Large source')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def picker(path):
    return f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\nkey ENTER\nwait_for_refresh\n'


def capture(name, count):
    return f'wait_frames 20\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += picker('large.cpp') + 'focus_ui diff_scroll\nkey CTRL+G\ntype "4097"\nkey ENTER\nwait_for_refresh\n' + capture('page_two', 2)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -25\n' + capture('scrolled', 2)
    script += picker('other.cpp') + 'click_ui open_tabs_menu\nwait_frames 3\nclick_ui context_menu_item_large.cpp\nwait_for_refresh\n' + capture('restored', 3)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    anchors = {}
    for name in ['page_two', 'scrolled', 'restored']:
        data = json.loads((directory / f'{name}.workspace.json').read_text())
        anchors[name] = data['history'][data['history_index']]['anchor']
    assert anchors['page_two']['line'] > 4096, anchors
    saved = anchors['scrolled']
    assert saved['line'] > anchors['page_two']['line'], anchors
    data = json.loads((directory / 'restored.json').read_text())
    viewport = next(n['rect'] for n in data['nodes'] if n.get('name') == 'diff_scroll' and n['rendered'])
    expected = viewport['y'] + saved['fraction'] * viewport['height']
    rows = [r for r in data['reading_rows'] if r['path'] == 'large.cpp' and r['line'] == saved['line']]
    assert rows and min(abs(r['rect']['y'] - expected) for r in rows) < 2, (saved, rows, viewport)
    print(f'PASS {zoom}%: page two, explicit scrolling, release payload, activate saved line {saved["line"]}', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
