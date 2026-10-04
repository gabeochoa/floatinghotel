import argparse
import json
import os
from pathlib import Path
import subprocess

# Sidebar Git controls (sync row, commit area, mode tabs) size their rows in
# logical pixels, so zoomed text stays inside them instead of overflowing rows
# that were sized as a fraction of the window height.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    subprocess.check_output(['git', '-C', str(repo), *args], text=True)

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Typography fixture'),
                ('config', 'user.email', 'type@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
(repo / 'a.txt').write_text('one\n')
git('add', '.')
git('commit', '-qm', 'base')
(repo / 'a.txt').write_text('two\n')

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1280 800\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'wait_for_refresh\nwait_frames 10\nscreenshot sidebar\n'
    (directory / 'journey.e2e').write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(ROOT / 'output/floatinghotel.exe'), str(repo), '--test-mode', '--headless', f'--test-script={directory / "journey.e2e"}',
            f'--screenshot-dir={directory}', '--e2e-timeout=120'], cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=180)
    assert result.returncode == 0, directory / 'run.log'
    snapshot = json.loads((directory / 'sidebar.json').read_text())
    scale = snapshot['ui_scale']
    nodes = {n['id']: n for n in snapshot['nodes']}
    checked = 0
    for row in [n for n in snapshot['nodes'] if n.get('name') in ('sync_row', 'commit_area', 'sidebar_mode_tabs')]:
        for child in (nodes[c] for c in row['children']):
            r, p = child['rect'], row['rect']
            assert p['y'] - 0.5 <= r['y'] and r['y'] + r['height'] <= p['y'] + p['height'] + 0.5, (zoom, row['name'], child.get('name'), r, p)
            if child.get('text'):
                assert r['height'] / scale >= child['font_size']['value'], (zoom, child['name'], r, child['font_size'])
            checked += 1
    assert checked >= 8, (zoom, checked)
    log = (directory / 'run.log').read_text()
    assert "parent 'sync_row'" not in log and "parent 'sidebar_mode_tabs'" not in log and "parent 'commit_area'" not in log, zoom
    print(f'PASS {zoom}% sync row, commit area and mode tabs hold their zoomed text ({checked} controls)', flush=True)
