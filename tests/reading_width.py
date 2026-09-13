import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from reading_journey import ROOT, fixture

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--native', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
fixture(repo)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
measurements = []
for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Reading change"\nwait_for_refresh\nscreenshot commit_wide\n'
    script += 'resize 1100 900\nwait_frames 8\nscreenshot commit_narrow\n'
    script += 'click_text "Split"\nwait_frames 8\nscreenshot split_narrow\n'
    script += 'resize 1600 1100\nwait_frames 8\nscreenshot split_wide\n'
    script += 'click_text "Unified"\nwait_frames 8\nhover_ui commit_detail_scroll\nscroll_wheel 0 20000\nwait_frames 8\n'
    script += 'click_ui open_full_file\nwait_for_refresh\nscreenshot source_wide\n'
    script += 'resize 1100 900\nwait_frames 8\nscreenshot source_narrow\nbench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    env = dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(directory / 'settings'))
    command = [str(binary), str(repo), '--test-mode', f'--test-script={path}',
               f'--screenshot-dir={directory}', '--e2e-timeout=120']
    if args.native:
        env['FH_TEST_NATIVE_HIDDEN'] = '1'
    else:
        command.append('--headless')
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=150)
    assert result.returncode == 0, directory / 'run.log'
    if args.native:
        assert 'Native test window hidden=1 key=0' in (directory / 'run.log').read_text()
    for name in ['commit_wide', 'commit_narrow', 'split_narrow', 'split_wide', 'source_wide', 'source_narrow']:
        data = json.loads((directory / f'{name}.json').read_text())
        nodes = {n['name']: n for n in data['nodes'] if n.get('name') and n['rendered']}
        edge = data['viewport']['width']
        for container in ['main_content', 'diff_scroll' if name.startswith('source') else 'commit_detail_scroll']:
            rect = nodes[container]['visible_rect']
            assert abs(rect['x'] + rect['width'] - edge) < 1, (zoom, name, container, rect, edge)
        rows = [n for n in data['nodes'] if n.get('name') in ['diff_line', 'sbs_cell'] and n['rendered']]
        assert rows, (zoom, name, 'No visible code')
        right = max(n['visible_rect']['x'] + n['visible_rect']['width'] for n in rows)
        assert edge - 10 * zoom / 100 <= right <= edge + 1, (zoom, name, right, edge)
        assert all(n['visible_rect']['x'] + n['visible_rect']['width'] <= edge + 1 for n in rows)
        if not args.native:
            assert (directory / f'{name}.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        measurements.append(dict(zoom=zoom, view=name, window_right=edge, code_right=right))
    print(f'PASS {zoom}%: unified, split and source reach the right edge at wide and narrow sizes', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(binary_sha256=digest, native=args.native, measurements=measurements), indent=2) + '\n')
