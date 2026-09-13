import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--snapshots', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Inbox fixture'),
                ('config', 'user.email', 'inbox@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
for name in ['a', 'b', 'c']:
    (repo / f'{name}.cpp').write_text(f'int {name}_value = 1;\n')
git('add', '.')
git('commit', '-qm', 'Older inbox')
for name in ['a', 'b', 'c']:
    (repo / f'{name}.cpp').write_text(f'int {name}_value = 2;\n')
git('add', '.')
git('commit', '-qm', 'Inbox fixture')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
def capture(name):
    return f'wait_for_refresh\nwait_frames 10\nworkspace_checkpoint 2 {name}\nscreenshot {name}\n'
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'
    settings.mkdir()
    (settings / 'settings.json').write_text(json.dumps(dict(window_width=1800, window_height=1000,
        window_shelf_collapsed=False, expanded_window_width=1800, sidebar_width=280)))
    dock = round(280 * zoom / 100)
    collapse = f'native_menu_action "Collapse reading panel"\nwait_window_size {dock} 1000\n'
    expand = 'wait_window_size 1800 1000\n'
    script = 'wait_for_refresh\nscreenshot initial\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Inbox fixture"\nwait_for_refresh\nkey ENTER\n' + capture('review')
    script += collapse + capture('inbox')
    script += 'hover_ui dock_review_remaining\n' + capture('hover_summary')
    script += 'hover_ui commit_row\n' + capture('hover_commit')
    script += 'click_ui dock_review_remaining\n' + expand + capture('first')
    script += 'click_ui approve_file_btn\n' + collapse + capture('two_left')
    script += 'click_ui dock_review_remaining\n' + expand + capture('second')
    script += 'click_text "Open file"\nwait_for_refresh\n' + 'wait_frames 10\n'
    script += collapse + 'wait_frames 10\nscreenshot source_origin\nclick_ui dock_review_remaining\n' + expand
    script += 'wait_for_refresh\nwait_frames 10\nscreenshot source_return\n'
    script += 'click_ui approve_file_btn\n' + collapse + 'wait_frames 10\nscreenshot one_left\n'
    script += 'click_ui dock_review_remaining\n' + expand + 'wait_for_refresh\nclick_ui approve_file_btn\n' + collapse
    script += 'wait_frames 10\nscreenshot complete\n'
    script += 'click_ui jump_to_diff:a.cpp\n' + expand + 'wait_frames 10\nscreenshot row_expands\n'
    script += 'click_ui review_staged_changes\nwait_for_refresh\n' + collapse + 'wait_frames 10\nscreenshot empty_index\n'
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    if args.snapshots:
        script = '\n'.join('resize ' + l.split(' ', 1)[1] if l.startswith('wait_window_size ') else l for l in script.splitlines()) + '\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    env = dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings))
    if not args.snapshots:
        env.update(FH_TEST_NATIVE_HIDDEN='1', FH_TEST_NATIVE_DOCK='1')
    command = [str(binary), str(repo), '--test-mode', f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180']
    if args.snapshots:
        command.append('--headless')
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def nodes(name):
        return {n['name']: n for n in layout(name)['nodes'] if n.get('name') and n['rendered']}
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    for name in ['hover_summary', 'hover_commit']:
        assert state(name)['tabs'] == state('inbox')['tabs'] and state(name)['active'] == state('inbox')['active']
        assert layout(name)['viewport']['width'] == dock and 'content_tabs' not in nodes(name)
    for name, remaining in [('inbox', 3), ('two_left', 2), ('source_origin', 2), ('one_left', 1)]:
        node = nodes(name)['dock_review_remaining']
        assert all('File approved for review' not in n.get('text', '') for n in layout(name)['nodes'] if n['rendered'])
        assert node['text'] == f'{remaining} left', (zoom, name, node['text'])
        assert node['rect']['height'] <= 28.1 * zoom / 100
        assert abs(node['rect']['width'] - node['visible_rect']['width']) < 1, (zoom, name)
        n = nodes(name)
        assert n['changed_files_header']['rect']['y'] < n['log_header']['rect']['y']
        footer = n['status_bar_bg']['rect']
        assert abs(footer['y'] + footer['height'] - layout(name)['viewport']['height']) < 1
    for name, path in [('first', 'a.cpp'), ('second', 'b.cpp'), ('source_return', 'b.cpp'), ('row_expands', 'a.cpp')]:
        headers = [n for n in layout(name)['nodes'] if n.get('name') == 'file_header_row' and n['rendered']]
        assert len(headers) == 1 and headers[0]['focus_target']['item'] == path, (zoom, name, headers)
        assert layout(name)['viewport']['width'] == 1800
    assert nodes('complete')['tree_review_progress']['text'] == '3 / 3'
    assert 'dock_review_remaining' not in nodes('complete')
    assert 'commit_file_filter' not in nodes('empty_index') and 'dock_review_remaining' not in nodes('empty_index')
    print(f'PASS {zoom}% dock progress, next remaining file, source origin, hover, row expansion and empty index', flush=True)
assert git('status', '--porcelain') == ''
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
