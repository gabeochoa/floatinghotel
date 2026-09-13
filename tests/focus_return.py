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
(repo / 'a.cpp').write_text(''.join(f'int value_{i} = {i};\n' for i in range(80)))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Focus fixture'),
                ('config', 'user.email', 'focus@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Focus fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
p = repo / 'a.cpp'
p.write_text(p.read_text().replace('value_10 = 10', 'value_10 = 1000'))
initial_diff = subprocess.check_output(['git', '-C', str(repo), 'diff'])
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name, count=2):
    return f'wait_frames 8\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Focus fixture"\nwait_for_refresh\nkey ENTER\nclick_ui content_document_2\n' + capture('caller')
    script += 'key CMD+P\nwait_frames 3\ntype "a.cpp"\n' + capture('picker') + 'key ESCAPE\n' + capture('picker_return')
    script += 'key CMD+F\nwait_frames 3\ntype "value"\n' + capture('find') + 'key ESCAPE\n' + capture('find_return')
    script += 'click_ui commit_file_filter\nwait_frames 4\nkey CMD+P\nwait_frames 4\nkey ESCAPE\n' + capture('tree_return')
    script += 'click_ui commit_row\nwait_frames 4\nkey CMD+P\nwait_frames 4\nkey ESCAPE\n' + capture('history_return')
    script += 'click_ui content_document_2\nwait_frames 4\n'
    script += 'click_ui open_tabs_menu\nwait_frames 3\nkey ESCAPE\n' + capture('menu_return')
    script += 'right_click_ui content_document_1\nwait_frames 3\nkey ESCAPE\n' + capture('inactive_menu_return')
    script += 'click_ui content_document_2\nkey CMD+SHIFT+F\nclick_ui repo_search_input\ntype "value_1"\nclick_ui repo_search_submit\nwait_for_refresh\nwait_frames 20\n'
    script += 'click_ui repo_search_preview\n' + capture('preview')
    script += 'click_ui repo_search_preview_close\n' + capture('preview_return')
    script += 'key ESCAPE\n' + capture('search_return')
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nwait_frames 6\n'
    script += 'click_ui comment_hunk_btn\nwait_frames 4\nclick_ui comment_input\ntype "Focus fixture feedback"\nclick_ui comment_add_btn\nwait_frames 10\nclick_ui basket_close\nwait_frames 6\n'
    script += 'click_ui basket_toggle_btn\n' + capture('feedback_open')
    script += 'click_ui basket_close\n' + capture('feedback_return')
    script += 'right_click_ui content_document_1\nwait_frames 3\nclick_ui "context_menu_item_Close"\nwait_frames 4\n' + capture('closed_invoker', 1)
    script += 'key CMD+P\nwait_frames 3\nnew_tab\nwait_frames 8\nscreenshot other_repository\nclose_tab\nwait_for_refresh\n' + capture('repository_return', 1)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'

    def focused(name):
        data = json.loads((directory / f'{name}.json').read_text())
        return [n for n in data['nodes'] if n['focused'] and n['rendered'] and not n['hidden']]

    for name, control, region in [('picker', 'file_picker_input', 'Picker'), ('find', 'diff_find_input', 'Find'),
        ('picker_return', 'content_document_2', 'DocumentTabs'), ('find_return', 'content_document_2', 'DocumentTabs'),
        ('tree_return', 'commit_file_filter', 'Tree'), ('history_return', 'commit_row', 'History'),
        ('menu_return', 'open_tabs_menu', 'DocumentTabs'), ('inactive_menu_return', 'content_document_1', 'DocumentTabs'),
        ('preview_return', 'repo_search_preview', 'Search'), ('search_return', 'content_document_2', 'DocumentTabs'),
        ('feedback_return', 'basket_toggle_btn', 'Code')]:
        nodes = focused(name)
        assert len(nodes) == 1 and nodes[0].get('focus_target', {}).get('control') == control, (zoom, name, [(n.get('name'), n.get('focus_target')) for n in nodes])
        assert nodes[0]['focus_target']['region'] == region, (zoom, name, nodes[0]['focus_target'])
        assert nodes[0]['focus_target']['repository'] == str(repo)
        assert nodes[0]['visible_rect']['width'] > 0 and nodes[0]['visible_rect']['height'] > 0
    for name, text in [('picker', 'a.cpp'), ('find', 'value')]:
        assert focused(name)[0].get('text') == text, (zoom, name, focused(name))
    before = json.loads((directory / 'caller.workspace.json').read_text())
    after = json.loads((directory / 'picker_return.workspace.json').read_text())
    assert before['active'] == after['active'] and before['history'] == after['history']
    for name in ['closed_invoker', 'repository_return']:
        assert all(n.get('focus_target', {}).get('document', 0) != 1 for n in focused(name)), (zoom, name)
    assert all(n.get('focus_target', {}).get('repository', '') != str(repo) for n in focused('other_repository'))
    print(f'PASS {zoom}%: picker, Find, menus, search preview, feedback, closed document, repository switch', flush=True)
assert subprocess.check_output(['git', '-C', str(repo), 'diff', '--cached']) == b''
assert subprocess.check_output(['git', '-C', str(repo), 'diff']) == initial_diff
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
