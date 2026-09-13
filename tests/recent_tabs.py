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
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
for name in 'abcdefgh':
    (repo / f'{name}.cpp').write_text(''.join(f'int {name}_{i} = {i};\n' for i in range(500)))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Recent fixture'),
                ('config', 'user.email', 'recent@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Recent sources')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
checks = {
    'single': (1, 1, None), 'plain_tab': (9, 9, None), 'before': (9, 9, None), 'first': (9, 9, 8), 'second': (9, 9, 7), 'reverse': (9, 9, 8),
    'committed': (9, 8, None), 'returning': (9, 8, 9), 'returned': (9, 9, None),
    'backward': (9, 9, 1), 'escaped': (9, 9, None), 'closed': (8, 9, None),
    'without_closed': (8, 9, 7), 'close_while_held': (7, 7, 7), 'released_after_close': (7, 7, None),
    'switch_repo': (1, 1, None), 'return_repo': (7, 7, None), 'narrow': (7, 7, 1), 'narrow_dismissed': (7, 7, None),
}


def capture(name):
    return f'workspace_checkpoint {checks[name][0]} {name}\nscreenshot {name}\n'


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'hold_key 345\nkey TAB\nrelease_key 345\nwait_frames 3\n' + capture('single')
    for name in 'abcdefgh':
        script += f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{name}.cpp"\nwait_frames 2\nkey ENTER\nwait_for_refresh\n'
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -20\nwait_frames 20\n' + capture('before')
    script += 'key TAB\nwait_frames 3\n' + capture('plain_tab')
    script += 'hold_key 341\nkey TAB\nwait_frames 3\n' + capture('first')
    script += 'key TAB\nwait_frames 3\n' + capture('second')
    script += 'hold_key 340\nkey TAB\nrelease_key 340\nwait_frames 3\n' + capture('reverse')
    script += 'release_key 341\nwait_for_refresh\n' + capture('committed')
    script += 'hold_key 345\nkey TAB\nwait_frames 3\n' + capture('returning')
    script += 'release_key 345\nwait_for_refresh\n' + capture('returned')
    script += 'hold_key 341\nhold_key 340\nkey TAB\nwait_frames 3\n' + capture('backward')
    script += 'key ESCAPE\nwait_frames 3\n' + capture('escaped')
    script += 'release_key 340\nrelease_key 341\nclick_ui close_document_8\nwait_frames 3\n' + capture('closed')
    script += 'hold_key 341\nkey TAB\nwait_frames 3\n' + capture('without_closed')
    script += 'key W\nwait_for_refresh\n' + capture('close_while_held')
    script += 'release_key 341\nwait_for_refresh\n' + capture('released_after_close')
    script += 'hold_key 341\nkey TAB\nwait_frames 3\nnew_tab\nwait_frames 3\n' + capture('switch_repo')
    script += 'release_key 341\nclose_tab\nwait_for_refresh\n' + capture('return_repo')
    script += 'resize 950 700\nwait_frames 5\nhold_key 341\nhold_key 340\nkey TAB\nwait_frames 3\n' + capture('narrow')
    script += 'key ESCAPE\nrelease_key 340\nrelease_key 341\nwait_frames 3\n' + capture('narrow_dismissed')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'],
            cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    saved = None
    for name, (count, active, selected) in checks.items():
        workspace = json.loads((directory / f'{name}.workspace.json').read_text())
        assert workspace['active'] == active and len(workspace['tabs']) == count and workspace['inactive_payloads_empty'], (zoom, name, workspace)
        data = json.loads((directory / f'{name}.json').read_text())
        nodes = [n for n in data['nodes'] if n['rendered'] and not n['hidden']]
        panels = [n for n in nodes if n.get('name') == 'recent_tabs']
        if selected is None:
            assert not panels, (zoom, name, 'Switcher outlived modifier')
        else:
            assert len(panels) == 1, (zoom, name, 'No held-modifier switcher')
            panel = panels[0]['rect']
            assert panel['x'] >= 0 and panel['y'] >= 0
            assert panel['x'] + panel['width'] <= data['viewport']['width'] + .2
            assert panel['y'] + panel['height'] <= data['viewport']['height'] + .2
            rows = [n for n in nodes if n.get('name', '').startswith('recent_tab_')]
            assert len(rows) <= 8
            chosen = next(n for n in rows if n.get('name') == f'recent_tab_selected_{selected}')
            assert chosen['visible_rect']['height'] >= chosen['rect']['height'] - .2
            pixel = Image.open(directory / f'{name}.png').convert('RGB').getpixel((int(chosen['rect']['x']+3), int(chosen['rect']['y']+chosen['rect']['height']/2)))
            assert pixel == (40, 48, 61), (zoom, name, pixel)
            for row in rows:
                assert int(row['name'].rsplit('_', 1)[1]) in [t['id'] for t in workspace['tabs']]
        if name in ['before', 'first', 'second', 'reverse', 'returned', 'backward', 'escaped', 'closed', 'without_closed']:
            offset = next(n for n in nodes if n.get('name') == 'diff_scroll')['scroll']['y']
            if saved is None: saved = offset
            assert saved > 100 and abs(offset - saved) < 1, (zoom, name, 'Underlying reader moved')
    print(f'PASS {zoom}%: frozen MRU order, reverse, modifier release, Escape, closed tabs, repository separation, narrow overlay, unchanged retained reading position', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + '\n')
