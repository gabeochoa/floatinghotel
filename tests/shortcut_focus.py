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
                ('add', '.'), ('commit', '-qm', 'Base fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
p = repo / 'a.cpp'
p.write_text(p.read_text().replace('value_10 = 10', 'value_10 = 1000').replace('value_50 = 50', 'value_50 = 5000'))
subprocess.run(['git', '-C', str(repo), 'commit', '-qam', 'Focus fixture'], check=True)
p.write_text(p.read_text().replace('value_10 = 1000', 'value_10 = 1001').replace('value_50 = 5000', 'value_50 = 5001'))
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
    script += 'click_text "Focus fixture"\nwait_for_refresh\nkey ENTER\nclick_ui content_document_2\n'
    script += 'key CMD+P\nwait_frames 3\ntype "a.cpp"\n' + capture('before')
    script += 'key ALT+LEFT\nkey ALT+RIGHT\nwait_frames 8\n' + capture('after')
    script += 'key ESCAPE\nclick_ui content_document_2\nkey J\n' + capture('commit_keys')
    script += 'key K\nkey A\nkey C\n' + capture('hidden_actions')
    script += 'click_ui open_tabs_menu\nkey J\nkey C\nkey A\n' + capture('menu_keys') + 'key ESCAPE\n'
    script += 'click_ui content_document_2\nkey CMD+F\nwait_frames 4\ntype "value"\n' + capture('find_before')
    script += 'key ALT+LEFT\nkey ALT+RIGHT\nkey CMD+A\nkey CMD+C\n' + capture('find_after')
    script += 'click_ui content_document_2\nkey ENTER\n' + capture('find_unfocused')
    script += 'key ESCAPE\nclick_ui commit_file_filter\nkey J\nkey C\nkey A\n' + capture('tree_keys')
    script += 'key CMD+A\nkey BACKSPACE\nclick_ui commit_row\nkey J\nkey C\nkey A\n' + capture('history_keys')
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nwait_frames 8\nclick_ui content_document_1\nkey J\n' + capture('reader_next') + 'key K\nclick_ui comment_hunk_btn\nwait_frames 4\nclick_ui comment_input\ntype "alpha beta"\n' + capture('comment_before')
    script += 'key ALT+LEFT\nkey ALT+RIGHT\nkey CMD+A\nkey CMD+C\n' + capture('comment_after')
    script += 'type "jac"\n' + capture('comment_typed')
    script += 'key CMD+SHIFT+F\nwait_frames 4\ntype "value"\n' + capture('search_before')
    script += 'key ALT+LEFT\nkey ALT+RIGHT\nkey ENTER\nwait_for_refresh\n' + capture('search_after')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def checkpoint(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())

    def focused(name):
        data = json.loads((directory / f'{name}.json').read_text())
        return [n for n in data['nodes'] if n['focused'] and n['rendered'] and not n['hidden']]

    for before_name, after_name in [('before', 'after'), ('find_before', 'find_after'), ('comment_before', 'comment_after'), ('search_before', 'search_after')]:
        before, after = checkpoint(before_name), checkpoint(after_name)
        assert before['active'] == after['active'] and before['history_index'] == after['history_index'], (zoom, after_name, 'Option+Arrow changed document while editing')
        assert before['review'] == after['review'], (zoom, after_name, 'Editing changed review', before['review'], after['review'])
        assert focused(after_name) and focused(before_name)[0]['focus_target'] == focused(after_name)[0]['focus_target'], (zoom, after_name)
    def find_count(name):
        return next(n['text'] for n in json.loads((directory / f'{name}.json').read_text())['nodes'] if n.get('name') == 'diff_find_count' and n['rendered'])
    assert find_count('find_unfocused') == find_count('find_after'), (zoom, 'Enter outside Find moved its match')
    assert checkpoint('reader_next')['review']['cursor'] == 1, (zoom, checkpoint('reader_next'))
    for name in ['hidden_actions', 'menu_keys', 'tree_keys', 'history_keys']:
        review = checkpoint(name)['review']
        assert review['cursor'] == 0 and not review['composing'] and not review['approved'] and not review['approve_pending'] and not review['comment_pending'], (zoom, name, review)
    assert checkpoint('comment_typed')['review']['draft'] == 'jac', (zoom, checkpoint('comment_typed'))
    assert not checkpoint('comment_typed')['review']['approved']
    print(f'PASS {zoom}%: text owns editing; reader owns review; hidden controls stay inactive', flush=True)
assert subprocess.check_output(['git', '-C', str(repo), 'diff', '--cached']) == b''
assert subprocess.check_output(['git', '-C', str(repo), 'diff']) == initial_diff
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
