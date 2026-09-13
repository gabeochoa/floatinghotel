import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Change fixture'),
                ('config', 'user.email', 'change@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
for name in ['a', 'b']:
    (repo / f'{name}.cpp').write_text(''.join(f'int {name}_{i} = {i};\n' for i in range(1, 101)))
(repo / 'z.bin').write_bytes(b'old\0binary')
git('add', '.')
git('commit', '-qm', 'Change navigation root')
base = git('rev-parse', 'HEAD')
p = repo / 'a.cpp'
p.write_text(p.read_text().replace('a_10 = 10', 'a_10 = 1000').replace('a_80 = 80', 'a_80 = 8000'))
p = repo / 'b.cpp'
p.write_text(p.read_text().replace('int b_20 = 20;\n', ''))
(repo / 'z.bin').write_bytes(b'new\0binary')
git('add', '.')
git('commit', '-qm', 'Change navigation fixture')
target = git('rev-parse', 'HEAD')
p = repo / 'a.cpp'
p.write_text(p.read_text().replace('a_10 = 1000', 'a_10 = 1001').replace('a_80 = 8000', 'a_80 = 8001'))
p = repo / 'b.cpp'
p.write_text(p.read_text().replace('int b_21 = 21;\n', ''))
(repo / 'z.bin').write_bytes(b'working\0binary')
before = git('diff')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def capture(name, count):
    return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:a.cpp\nclick_ui hunk_header_label\n' + capture('working_start', 1)
    script += 'key J\n' + capture('working_second', 1)
    script += 'key J\n' + capture('working_deleted', 1)
    if not args.baseline:
        script += 'key J\n' + capture('working_binary', 1)
        script += 'key J\n' + capture('last', 1) + 'expect_text "Last change"\n'
        script += 'key K\nkey K\nkey K\n' + capture('working_first', 1)
        script += 'key K\n' + capture('first', 1) + 'expect_text "First change"\n'
    script += 'click_text "Change navigation fixture"\nwait_for_refresh\nkey ENTER\nclick_ui jump_to_diff:a.cpp\nclick_ui hunk_header_label\n' + capture('commit_start', 2)
    script += ('key J\n' if args.baseline else 'native_menu_action "Next Change"\n') + capture('commit_second', 2)
    script += 'key J\n' + capture('commit_deleted', 2)
    if not args.baseline:
        script += 'native_menu_action "Previous Change"\n' + capture('commit_previous', 2)
        script += 'key CMD+F\nwait_frames 3\nkey J\nkey K\ntype "jk"\n' + capture('find', 2)
        script += 'key ESCAPE\n'
        script += 'native_menu_action "Compare Revisions..."\nwait_frames 3\n'
        script += f'click_ui compare_base\nkey CMD+A\ntype "{base}"\nclick_ui compare_target\nkey CMD+A\ntype "{target}"\nclick_ui compare_submit\nwait_for_refresh\nwait_frames 20\n'
        script += 'click_ui jump_to_diff:a.cpp\nclick_ui hunk_header_label\nkey J\n' + capture('comparison_second', 3)
        script += 'key J\n' + capture('comparison_deleted', 3)
        script += 'right_click_ui jump_to_diff:b.cpp\nwait_frames 3\nclick_ui "context_menu_item_Open source"\nwait_for_refresh\n' + capture('deleted_source', 4)
        script += 'click_ui full_file_back\nwait_for_refresh\nnative_menu_action "Next Change"\n' + capture('comparison_binary', 4)
        script += 'native_menu_action "Previous Change"\nwait_frames 15\nbench_frames 120\nexpect_p99_below 20\nclick_ui review_staged_changes\nwait_for_refresh\nclick_ui working_review_heading\nkey A\nkey C\nnative_menu_action "Next Change"\n' + capture('empty', 4)
        script += 'expect_text "No changes to navigate"\nbench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, (zoom, directory / 'run.log')
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def current(name):
        value = state(name)
        return value['history'][value['history_index']]
    if not args.baseline:
        for name, path, line in [('working_second', 'a.cpp', 80), ('working_deleted', 'b.cpp', 20),
                                  ('working_first', 'a.cpp', 10), ('commit_second', 'a.cpp', 80),
                                  ('commit_deleted', 'b.cpp', 20), ('commit_previous', 'a.cpp', 80),
                                  ('comparison_second', 'a.cpp', 80), ('comparison_deleted', 'b.cpp', 20)]:
            anchor = current(name)['anchor']
            assert anchor['path'] == path, (zoom, name, anchor)
            rows = [r for r in layout(name)['reading_rows'] if r['path'] == path and r['line'] == line]
            assert rows, (zoom, name, anchor)
            viewport = next(n['visible_rect'] for n in layout(name)['nodes'] if n.get('name') in ['diff_scroll', 'commit_detail_scroll'] and n['rendered'])
            assert any(viewport['y'] <= r['rect']['y'] < viewport['y'] + viewport['height'] for r in rows), (zoom, name, rows, viewport)
            focus = [n['focus_target']['region'] for n in layout(name)['nodes'] if n['focused'] and 'focus_target' in n]
            assert focus == ['Code'], (zoom, name, focus)
        for name in ['working_binary', 'comparison_binary']:
            assert any(n.get('name') == 'file_header_row' and n.get('focus_target', {}).get('item') == 'z.bin' and n['visible_rect']['height'] > 0 for n in layout(name)['nodes'])
        assert current('find')['anchor'] == current('commit_previous')['anchor']
        assert state('last')['history'] == state('working_binary')['history']
        assert state('first')['history'] == state('working_first')['history']
        assert not state('empty')['review']['approve_pending'] and not state('empty')['review']['comment_pending']
        source = next(t for t in state('deleted_source')['tabs'] if t['id'] == state('deleted_source')['active'])
        assert source['path'] == 'b.cpp' and source['revision'] == base
    assert git('diff') == before and git('diff', '--cached') == ''
    print(f'PASS {zoom}% change navigation' + (' baseline' if args.baseline else ', working/commit/comparison, deleted side, file boundaries, endpoints and focus'), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest), indent=2) + '\n')
