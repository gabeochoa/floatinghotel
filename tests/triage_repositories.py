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
parser.add_argument('--zooms', nargs='+', type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'
    (settings / 'reviews').mkdir(parents=True)
    def git(repo, *arguments):
        return subprocess.check_output(['git', '-C', str(repo), *arguments], text=True, stderr=subprocess.STDOUT).strip()
    def fixture(path):
        path.mkdir()
        git(path, 'init', '-q', '-b', 'main')
        git(path, 'config', 'user.name', 'Repository fixture')
        git(path, 'config', 'user.email', 'reader@example.invalid')
        git(path, 'config', 'commit.gpgsign', 'false')
        (path / 'code.txt').write_text('Saved reading state\n')
        git(path, 'add', '.')
        git(path, 'commit', '-qm', 'Repository fixture')
        return git(path, 'rev-parse', 'HEAD')
    active, old, moved = (directory / name for name in ['active-repo', 'relink-original', 'relink-moved'])
    fixture(active)
    head = fixture(old)
    old.rename(moved)
    fetch, push = directory / 'fetch.git', directory / 'push.git'
    subprocess.check_call(['git', 'init', '--bare', '-q', str(fetch)])
    subprocess.check_call(['git', 'init', '--bare', '-q', str(push)])
    git(active, 'remote', 'add', 'server', str(fetch))
    git(active, 'remote', 'set-url', '--push', 'server', str(push))
    git(active, 'config', 'branch.main.pushRemote', 'server')
    state = {'schema_version': 1, 'recent_repos': [str(active), str(old)], 'pinned_repos': [str(old)],
             'repository_heads': {str(old): head}, 'collapsed_sections': {str(old): ['changes']}}
    (settings / 'settings.json').write_text(json.dumps(state))
    key = 'v1-' + hashlib.sha256(str(old).encode()).hexdigest()
    review = {'schema_version': 2, 'repo_path': str(old), 'comments': [
        {'scope': 'working', 'file': 'code.txt', 'line': 1, 'text': 'Retain this feedback'}]}
    original = settings / 'reviews' / f'{key}.json'
    original.write_text(json.dumps(review))
    original_bytes = original.read_bytes()
    def capture(name):
        return f'wait_frames 3\nwait_for_refresh\nwait_frames 15\nscreenshot {name}\n'
    script = 'resize 1800 1400\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'native_menu_action "Push"\n' + capture('push_destination')
    script += 'key ESCAPE\nwait_frames 10\nclick_ui repository_navigation\nwait_frames 4\nclick_ui "context_menu_item_Pin repository"\n'
    script += 'new_tab\nwait_frames 10\n' + capture('repositories')
    script += 'click_ui repository_picker_sort\nclick_ui repository_picker_query\ntype "relink"\n' + capture('filtered')
    script += 'right_click_ui recent_repo_relink-original\nwait_frames 4\nclick_ui "context_menu_item_Relink moved repository"\n'
    script += 'click_ui relink_repository_path\ntype "' + str(moved) + '"\n' + capture('relink_dialog')
    script += 'click_ui relink_repository_confirm\n' + capture('relinked')
    script += 'save_window_state\nclick_ui recent_repo_relink-moved\n' + capture('opened')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(active), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings), FH_TEST_PERSIST_REVIEW='1'),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def nodes(name, debug):
        return [n for n in json.loads((directory / f'{name}.json').read_text())['nodes'] if n['rendered'] and n.get('name') == debug]
    for name, controls in [('push_destination', ['push_destination_branch', 'push_confirm_destination']),
                           ('relink_dialog', ['relink_repository_path', 'relink_repository_confirm'])]:
        for control in controls:
            node = nodes(name, control)[0]
            rect, visible = node['rect'], node['visible_rect']
            assert rect['x'] >= 0 and rect['y'] >= 0
            assert rect['x'] + rect['width'] <= 1800 and rect['y'] + rect['height'] <= 1400
            assert abs(rect['height'] - visible['height']) < 1 and abs(rect['width'] - visible['width']) < 1, (zoom, name, control, rect, visible)
    for name in ['push_destination', 'relink_dialog']:
        modal = nodes(name, 'modal')[0]
        rect = modal['rect']
        pixel = Image.open(directory / f'{name}.png').getpixel((int(rect['x'] + 15 * zoom / 100), int(rect['y'] + rect['height'] / 2)))
        assert all(abs(pixel[i] - modal['background'][i]) <= 1 for i in range(3)), (zoom, name, pixel, modal['background'])
    assert nodes('push_destination', 'push_destination_branch')
    assert nodes('push_destination', 'push_confirm_destination')
    assert len(nodes('repositories', 'recent_name')) == 2
    assert len(nodes('filtered', 'recent_name')) == 1
    assert nodes('relinked', 'recent_repo_relink-moved')
    assert nodes('opened', 'repo_header_label')[0]['text'] == 'relink-moved'
    saved = json.loads((settings / 'settings.json').read_text())
    assert str(old) not in saved['recent_repos'] and str(moved) in saved['recent_repos']
    assert str(active) in saved['pinned_repos'] and str(moved) in saved['pinned_repos']
    assert saved['collapsed_sections'][str(moved)] == ['changes']
    assert original.read_bytes() == original_bytes
    copies = [p for p in (settings / 'reviews').glob('*.json') if json.loads(p.read_text()).get('repo_path') == str(moved)]
    assert len(copies) == 1 and json.loads(copies[0].read_text())['comments'][0]['text'] == 'Retain this feedback'
    assert git(active, 'status', '--porcelain') == '' and git(moved, 'status', '--porcelain') == ''
    assert git(push, 'for-each-ref') == ''
    print(f'PASS {zoom}% explicit push preview, repository pins/filter/relink and retained feedback', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps({'passed': True, 'binary_sha256': digest}, indent=2) + '\n')
