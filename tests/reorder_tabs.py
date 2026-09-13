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
parser.add_argument('--resume', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=args.resume)
repo = out / 'fixture'
if not args.resume:
    repo.mkdir()
    for name in 'abcdefgh':
        (repo / f'{name}.cpp').write_text(''.join(f'int {name}_{i} = {i};\n' for i in range(500)))
    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Reorder fixture'),
                    ('config', 'user.email', 'reorder@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                    ('add', '.'), ('commit', '-qm', 'Eight sources')]:
        subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
if args.resume:
    assert (out / 'binary.sha256').read_text() == digest
else:
    (out / 'binary.sha256').write_text(digest)


def capture(name):
    return f'workspace_checkpoint 9 {name}\nscreenshot {name}\n'


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir(exist_ok=args.resume)
    setup = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    setup += 'native_menu_action "Zoom In"\n' * steps
    for name in 'abcdefgh':
        setup += f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{name}.cpp"\nwait_frames 2\nkey ENTER\nwait_for_refresh\n'
    setup += 'hover_ui diff_scroll\nscroll_wheel 0 -20\nwait_frames 20\n' + capture('before')

    def replay(name, script):
        path = directory / f'{name}.e2e'
        complete = directory / ('before.json' if name == 'probe' else 'resized.json')
        if args.resume and path.exists() and path.read_text() == script and complete.exists():
            return
        path.write_text(script)
        with (directory / f'{name}.log').open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
                f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'],
                cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
        assert result.returncode == 0, directory / f'{name}.log'

    def snapshot(name):
        return json.loads((directory / f'{name}.json').read_text())

    def node(name, label):
        return next(n for n in snapshot(name)['nodes'] if n.get('name') == label)

    replay('probe', setup)
    scale = zoom / 100
    viewport = node('before', 'content_tab_viewport')['rect']
    last = node('before', 'content_document_9')['rect']
    close = node('before', 'close_document_9')['rect']
    previous = node('before', 'content_document_8')['rect']
    left, right = viewport['x'] + 2 * scale, viewport['x'] + viewport['width'] - 2 * scale
    y = viewport['y'] + viewport['height'] / 2
    start = last['x'] + 50 * scale
    first = viewport['x'] + 50 * scale
    script = setup
    script += f'mouse_down {start} {y}\nwait_frames 3\nmouse_move {left} {y}\nwait 5\n' + capture('left_drag')
    script += 'mouse_up\nwait_frames 5\n' + capture('first')
    script += f'mouse_down {first} {y}\nwait_frames 3\nmouse_move {right} {y}\nwait 5\n' + capture('right_drag')
    script += 'mouse_up\nwait_frames 5\n' + capture('last')
    script += f'mouse_down {start} {y}\nwait_frames 3\nmouse_move {start-30*scale} {y}\nwait_frames 3\nkey ESCAPE\nwait_frames 3\n' + capture('escape_held')
    script += 'mouse_up\nwait_frames 5\n' + capture('escaped')
    script += f'mouse_down {start} {y}\nwait_frames 3\nmouse_move 1599 999\nwait_frames 5\n' + capture('outside_held')
    script += 'mouse_up\nwait_frames 5\n' + capture('outside')
    script += f'mouse_down {start} {y}\nwait_frames 3\nmouse_move {close["x"]+close["width"]/2} {y}\nwait_frames 3\nmouse_up\nwait_frames 5\n' + capture('over_close')
    script += f'mouse_down {start} {y}\nwait_frames 3\nmouse_move {start+2*scale} {y}\nmouse_up\nwait_frames 5\n' + capture('jitter')
    script += f'mouse_down {previous["x"]+50*scale} {y}\nmouse_move {close["x"]+close["width"]/2} {y}\nwait_frames 3\nmouse_up\nwait_frames 5\n' + capture('inactive_moved')
    script += f'mouse_down {start} {y}\nmouse_move {previous["x"]+10*scale} {y}\nwait_frames 3\nmouse_up\nwait_frames 5\n' + capture('inactive_restored')
    script += f'mouse_down {start} {y}\nmouse_move {start-30*scale} {y}\nwait_frames 3\nresize 1200 800\nwait_frames 5\nmouse_up\nwait_frames 5\n' + capture('resized')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    replay('journey', script)
    baseline = json.loads((directory / 'before.workspace.json').read_text())
    baseline_offset = node('before', 'diff_scroll')['scroll']['y']
    for name in ['before', 'left_drag', 'first', 'right_drag', 'last', 'escape_held', 'escaped',
                 'outside_held', 'outside', 'over_close', 'jitter', 'inactive_moved', 'inactive_restored', 'resized']:
        workspace = json.loads((directory / f'{name}.workspace.json').read_text())
        expected = [9, *range(1, 9)] if name in ['first', 'right_drag'] else list(range(1, 10))
        if name == 'inactive_moved':
            expected = [*range(1, 8), 9, 8]
        assert [t['id'] for t in workspace['tabs']] == expected, (zoom, name, workspace)
        assert workspace['active'] == 9 and workspace['inactive_payloads_empty'], (zoom, name, workspace)
        assert sorted(workspace['tabs'], key=lambda t: t['id']) == baseline['tabs']
        assert abs(node(name, 'diff_scroll')['scroll']['y'] - baseline_offset) < 1, (zoom, name, 'Reader scrolled')
        data = snapshot(name)
        assert data['viewport'] == ({'width': 1200, 'height': 800} if name == 'resized' else snapshot('before')['viewport'])
        marker = [n for n in data['nodes'] if n.get('name') == 'document_tab_insertion' and n['rendered']]
        if name in ['left_drag', 'right_drag']:
            assert len(marker) == 1
            rect = marker[0]['visible_rect']
            assert abs(rect['width'] - 2 * scale) < .2 and rect['height'] > 20 * scale
            pixels = Image.open(directory / f'{name}.png').convert('RGB')
            center = (int(rect['x'] + rect['width'] / 2), int(rect['y'] + rect['height'] / 2))
            assert pixels.getpixel(center) == (185, 207, 239), (zoom, name, pixels.getpixel(center))
        else:
            assert not marker, (zoom, name)
    assert node('left_drag', 'content_tab_viewport')['scroll']['x'] < .2
    strip = node('right_drag', 'content_tab_viewport')
    assert abs(strip['scroll']['x'] - strip['scroll']['content_width'] + strip['rect']['width']) < 1
    print(f'PASS {zoom}%: both reorder directions, edge scrolling, visible marker, Escape, outside release, close suppression, jitter, resize cancellation, stable document and reading position', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + '\n')
