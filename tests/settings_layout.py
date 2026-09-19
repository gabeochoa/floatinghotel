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
(repo / 'a.cpp').write_text('int main() { return 0; }\n')
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Settings fixture'),
                ('config', 'user.email', 'settings@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Settings fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
results = []
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'
    settings.mkdir()
    (settings / 'settings.json').write_text(json.dumps(dict(window_width=1800, window_height=1100,
        window_shelf_collapsed=False, expanded_window_width=1800, sidebar_width=280)))
    setup = 'wait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    setup += 'click_text "Settings fixture"\nwait_for_refresh\nkey ENTER\n'
    setup += 'native_menu_action "Toggle Command Log"\nwait_frames 10\nscreenshot initial\n'
    def replay(name, script):
        path = directory / (name + '.e2e')
        path.write_text(script)
        with (directory / (name + '.log')).open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
                f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=90'],
                cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings)),
                stdout=log, stderr=subprocess.STDOUT, timeout=120)
        assert result.returncode == 0, (zoom, name)
    def snapshot(name):
        return json.loads((directory / (name + '.json')).read_text())
    def node(name, key):
        return next(n for n in snapshot(name)['nodes'] if n.get('name') == key and n['rendered'])
    replay('probe', setup)
    handle = node('initial', 'cmdlog_drag_handle')['visible_rect']
    initial = node('initial', 'cmdlog_panel')['rect']
    x, y = handle['x'] + handle['width'] / 2, handle['y'] + handle['height'] / 2
    script = setup + f'drag_to {x} {y} {x} {y - 40}\nwait_frames 15\nscreenshot dragged\n'
    script += 'native_menu_action "Collapse reading panel"\nwait_frames 15\nscreenshot dock\n'
    script += 'native_menu_action "Expand reading panel"\nwait_frames 15\nscreenshot expanded\n'
    script += 'resize 1250 700\nwait_frames 15\nscreenshot narrow\nresize 1800 1100\nwait_frames 15\nscreenshot wide\nsave_window_state\n'
    replay('save', script)
    dragged = node('dragged', 'cmdlog_panel')['rect']
    assert abs(dragged['height'] - initial['height'] - 40) < 3, (zoom, initial, dragged)
    assert not any(n.get('name') == 'cmdlog_panel' and n['rendered'] for n in snapshot('dock')['nodes'])
    for name in ['expanded', 'wide']:
        value = node(name, 'cmdlog_panel')['rect']
        assert abs(value['height'] - dragged['height']) < 1, (zoom, name, value, dragged)
    for name in ['dragged', 'expanded', 'narrow', 'wide']:
        panel = node(name, 'cmdlog_panel')['rect']
        footer = node(name, 'status_bar_bg')['rect']
        assert abs(panel['y'] + panel['height'] - footer['y']) < 1, (zoom, name, panel, footer)
        assert panel['y'] >= 0 and panel['height'] > 0
    saved = json.loads((settings / 'settings.json').read_text())
    assert abs(saved['command_log_height'] * zoom / 100 - dragged['height']) < 1, (zoom, saved, dragged)
    replay('restart', setup + 'screenshot restored\n')
    restored = node('restored', 'cmdlog_panel')['rect']
    assert abs(restored['height'] - dragged['height']) < 1, (zoom, restored, dragged)
    results.append(dict(zoom=zoom, remembered_logical_height=saved['command_log_height'],
        rendered_height=restored['height'], passed=True))
    print(f'PASS {zoom}%: drag, dock, narrow window and process restart retain command-log height', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, results=results), indent=2) + '\n')
