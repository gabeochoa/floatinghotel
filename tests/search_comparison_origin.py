import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--keyboard', action='store_true')
options = parser.parse_args()
out = options.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
(repo / 'gone.cpp').write_text(''.join(f'int NEEDLE_{i} = {i};\n' for i in range(1, 81)))
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Search origin fixture'),
             ('config', 'user.email', 'origin@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Comparison base')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
base = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
subprocess.run(['git', '-C', str(repo), 'rm', 'gone.cpp'], check=True, capture_output=True)
subprocess.run(['git', '-C', str(repo), 'commit', '-qm', 'Delete before final target'], check=True)
(repo / 'c.cpp').write_text('int current = 1;\n')
subprocess.run(['git', '-C', str(repo), 'add', '.'], check=True)
subprocess.run(['git', '-C', str(repo), 'commit', '-qm', 'Final target'], check=True)
target = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'native_menu_action "Compare Revisions..."\nwait_frames 3\n'
    script += f'click_ui compare_base\nkey CMD+A\ntype "{base}"\nclick_ui compare_target\nkey CMD+A\ntype "{target}"\nclick_ui compare_submit\nwait_for_refresh\nwait_frames 20\n'
    script += 'click_ui open_full_file\nwait_for_refresh\nwait_frames 20\nworkspace_checkpoint 3 source\nscreenshot source\n'
    script += 'key CMD+SHIFT+F\nwait_frames 3\nclick_ui repo_search_options\nclick_ui repo_search_changed_only\nclick_ui repo_search_input\ntype "NEEDLE"\nkey ENTER\nwait_for_refresh\nwait_frames 20\nworkspace_checkpoint 3 results\nscreenshot results\n'
    script += ('key DOWN\n' if options.keyboard else 'click_ui repo_search_result\n') + 'wait_for_refresh\nwait_frames 20\nworkspace_checkpoint 3 deleted\nscreenshot deleted\n'
    script += 'click_ui full_file_back\nwait_for_refresh\nwait_frames 20\nworkspace_checkpoint 3 returned\nscreenshot returned\nbench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, (result.returncode, directory / 'run.log')
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    results = state('results')
    assert results['search']['matches'] == 80 and results['search']['revision'] == target, (zoom, results['search'])
    deleted = state('deleted')
    active = next(t for t in deleted['tabs'] if t['id'] == deleted['active'])
    assert active['path'] == 'gone.cpp' and active['revision'] == base, (zoom, active)
    source = deleted['history'][deleted['history_index']]['location']['source']
    assert source['origin']['kind'] == 'comparison' and source['origin']['before']['value'] == base and source['origin']['after']['value'] == target, source
    rendered = json.loads((directory / 'deleted.json').read_text())['reading_rows']
    assert any(row['path'] == 'gone.cpp' and row['line'] == source['line'] for row in rendered), (zoom, 'Deleted source line not rendered')
    returned = state('returned')
    assert next(t for t in returned['tabs'] if t['id'] == returned['active'])['kind'] == 'review'
    assert returned['search']['submissions'] == 1 and returned['search']['matches'] == 80
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
    print(f'PASS {zoom}%: search from comparison source keeps actual base, deleted revision, origin and retained results', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, keyboard=options.keyboard), indent=2) + '\n')
