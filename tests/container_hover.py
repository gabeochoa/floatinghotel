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
(repo / 'a.cpp').write_text('int a = 1;\n')
(repo / 'b.cpp').write_text('int b = 1;\n')
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Hover fixture'),
             ('config', 'user.email', 'hover@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Hover fixture')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
(repo / 'a.cpp').write_text('int a = 2;\n')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
measurements = {}
for populated in [False, True]:
    if populated:
        (repo / 'b.cpp').write_text('int b = 2;\n')
        subprocess.run(['git', '-C', str(repo), 'add', 'b.cpp'], check=True)
    for zoom in [100, 140, 200]:
        label = f"{zoom}-{'populated' if populated else 'empty'}"
        directory = out / label
        directory.mkdir()
        script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        for scope in ['unstaged', 'staged']:
            script += f'click_ui review_{scope}_changes\nwait_for_refresh\nmouse_move 1750 15\nwait_frames 10\nscreenshot {scope}_neutral\n'
            for target in ['main_content', 'sidebar_bg', 'history_branch']:
                move = {'main_content': 'mouse_move 1700 950', 'sidebar_bg': 'mouse_move 10 600'}.get(target, f'hover_ui {target}')
                script += f'{move}\nwait_frames 8\nscreenshot {scope}_{target}\n'
        script += 'bench_frames 120\nexpect_p99_below 20\n'
        path = directory / 'journey.e2e'
        path.write_text(script)
        with (directory / 'run.log').open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
                f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'],
                cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
        assert result.returncode == 0, directory
        measurements[label] = {}
        for scope in ['unstaged', 'staged']:
            before = Image.open(directory / f'{scope}_neutral.png').convert('RGB')
            for target in ['main_content', 'sidebar_bg', 'history_branch']:
                name = f'{scope}_{target}'
                data = json.loads((directory / f'{name}.json').read_text())
                hot = [n for n in data['nodes'] if n['rendered'] and n['hot']]
                assert len(hot) == 1, (label, name, hot)
                node = hot[0]
                assert node.get('name') in ['main_content', 'diff_scroll', 'commit_detail_scroll', 'sidebar_bg', 'commit_log_scroll', 'commit_files_scroll'], (label, name, node.get('name'))
                rect = node['visible_rect']
                point = (int(rect['x'] + 5), int(rect['y'] + rect['height'] - 10))
                after = Image.open(directory / f'{name}.png').convert('RGB')
                assert before.getpixel(point) == after.getpixel(point), (label, name, point, before.getpixel(point), after.getpixel(point))
                measurements[label][name] = {'hot': node.get('name'), 'sample': point, 'rgb': after.getpixel(point)}
        print(f'PASS {label}: sidebar, history, and reader containers keep their backgrounds in both scopes', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, measurements=measurements), indent=2) + '\n')
