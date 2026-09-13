import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--zooms', type=int, nargs='+', default=[100, 140, 200])
parser.add_argument('--snapshots', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
(repo / 'a.cpp').write_text(''.join(f'int value_{i:04} = {i};\n' for i in range(400)))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Dock fixture'),
                ('config', 'user.email', 'dock@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Dock fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name):
    return f'wait_frames 10\nworkspace_checkpoint 3 {name}\nscreenshot {name}\n'


for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'
    settings.mkdir()
    (settings / 'settings.json').write_text(json.dumps(dict(window_width=1800, window_height=1100,
        window_shelf_collapsed=False, expanded_window_width=1800, sidebar_width=280)))
    scale = zoom / 100
    dock = round(280 * scale)
    script = 'wait_for_refresh\nscreenshot initial\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * round((zoom - 100) / 10)
    script += 'click_text "Dock fixture"\nwait_for_refresh\nkey ENTER\n'
    script += 'key CMD+P\nwait_frames 3\ntype "a.cpp"\nwait_for_refresh\nkey ENTER\nwait_for_refresh\n'
    script += 'key CTRL+G\nwait_frames 3\ntype "150:5"\nkey ENTER\nwait_for_refresh\nfocus_ui diff_scroll\nkey SHIFT+RIGHT\n' + capture('before')
    for i in range(3):
        script += f'native_menu_action "Collapse reading panel"\nwait_window_size {dock} 1100\n' + capture(f'dock_{i}')
        script += 'native_menu_action "Expand reading panel"\nwait_window_size 1800 1100\n' + capture(f'expanded_{i}')
    script += 'native_window_size 1500 950\n' + capture('manual_expanded')
    script += f'native_menu_action "Collapse reading panel"\nwait_window_size {dock} 950\n' + capture('manual_dock')
    script += f'native_window_size {dock + 40} 950\n' + capture('wider_dock')
    script += 'native_menu_action "Expand reading panel"\nwait_window_size 1540 950\n' + capture('wider_expanded')
    script += 'native_menu_action "Collapse reading panel"\nnative_menu_action "Expand reading panel"\nnative_menu_action "Collapse reading panel"\n'
    script += f'wait_window_size {dock + 40} 950\n' + capture('rapid_dock')
    script += 'native_menu_action "Expand reading panel"\nwait_window_size 1540 950\n' + capture('rapid_expanded')
    script += 'save_window_state\nbench_frames 120\nexpect_p99_below 20\n'
    if args.snapshots:
        script = '\n'.join(('resize ' + line.split(' ', 1)[1] if line.startswith(('native_window_size ', 'wait_window_size ')) else line)
                           for line in script.splitlines()) + '\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    env = dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings), FH_RESIZE_TIMING='1')
    if not args.snapshots:
        env.update(FH_TEST_NATIVE_HIDDEN='1', FH_TEST_NATIVE_DOCK='1')
    command = [str(binary), str(repo), '--test-mode', f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180']
    if args.snapshots:
        command.append('--headless')
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    if not args.snapshots:
        assert 'Native test window hidden=1 key=0' in (directory / 'run.log').read_text()
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    before = state('before')
    selected = next(t for t in before['tabs'] if t['id'] == before['active'])['selection']
    assert (selected['anchor_line'], selected['anchor_column'], selected['head_column']) == (150, 5, 6), selected
    for name in [f'{kind}_{i}' for i in range(3) for kind in ['dock', 'expanded']] + ['manual_expanded', 'manual_dock', 'wider_dock', 'wider_expanded', 'rapid_dock', 'rapid_expanded']:
        value = state(name)
        assert value['active'] == before['active'] and value['history_index'] == before['history_index'], (zoom, name)
        assert value['tabs'] == before['tabs'], (zoom, name, 'selection or document changed')
        view = layout(name)
        nodes = {n['name']: n for n in view['nodes'] if n.get('name') and n['rendered']}
        footer = nodes['status_bar_bg']['rect']
        assert abs(footer['y'] + footer['height'] - view['viewport']['height']) < 1, (zoom, name, footer)
        if 'dock' in name:
            assert 'content_tabs' not in nodes and 'full_file_header' not in nodes, (zoom, name)
            assert abs(nodes['sidebar_bg']['rect']['width'] - view['viewport']['width']) < 1, (zoom, name)
    for start, end in [('before', f'expanded_{i}') for i in range(3)] + [('wider_expanded', 'rapid_expanded')]:
        a, b = state(start), state(end)
        aa, bb = a['history'][a['history_index']]['anchor'], b['history'][b['history_index']]['anchor']
        for key in ['path', 'revision', 'line', 'column', 'side']:
            assert aa[key] == bb[key], (zoom, start, end, aa, bb)
        assert abs(aa['fraction'] - bb['fraction']) < .003, (zoom, start, end, aa, bb)
        view = layout(end)
        viewport = next(n['rect'] for n in view['nodes'] if n.get('name') == 'diff_scroll' and n['rendered'])
        expected = viewport['y'] + viewport['height'] * aa['fraction']
        rows = [r for r in view['reading_rows'] if r['path'] == aa['path'] and r['line'] == aa['line']]
        assert rows and min(abs(r['rect']['y'] - expected) for r in rows) < 2, (zoom, end, aa, rows)
    saved = json.loads((settings / 'settings.json').read_text())
    assert saved['window_width'] == 1540 and saved['window_height'] == 950 and saved['expanded_window_width'] == 1540, saved
    print(f'PASS {zoom}% dock cycles, manual widths, reading identity, selection, anchors and fixed footer', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
