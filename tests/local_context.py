import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Context fixture'),
                ('config', 'user.email', 'context@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
for name in ['a', 'b', 'c']:
    (repo / f'{name}.cpp').write_text(''.join(f'int {name}_{i} = {i}; // éλ\n' for i in range(1, 181)))
p = repo / 'c.cpp'
p.write_text(p.read_text().replace('int c_35 = 35; // éλ', 'x' * 300000))
git('add', '.')
git('commit', '-qm', 'Context root')
base = git('rev-parse', 'HEAD')
p = repo / 'a.cpp'
p.write_text(p.read_text().replace('a_30 = 30', 'a_30 = 3000').replace('a_60 = 60', 'a_60 = 6000').replace('a_150 = 150', 'a_150 = 15000'))
p = repo / 'c.cpp'
p.write_text(p.read_text().replace('c_30 = 30', 'c_30 = 3000').replace('c_60 = 60', 'c_60 = 6000'))
p = repo / 'b.cpp'
p.write_text(p.read_text().replace('b_10 = 10', 'b_10 = 1000'))
git('add', '.')
git('commit', '-qm', 'Local context fixture')
target = git('rev-parse', 'HEAD')
for name in ['a', 'b', 'c']:
    p = repo / f'{name}.cpp'
    p.write_text(p.read_text().replace('= 3000;', '= 3001;').replace('= 6000;', '= 6001;').replace('= 15000;', '= 15001;').replace('= 1000;', '= 1001;'))
before = git('diff')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def settle():
    return 'wait_frames 3\nwait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\n'

def capture(name, count):
    return settle() + f'workspace_checkpoint {count} {name}\nscreenshot {name}\n'

def expand(direction):
    return f'right_click_ui hunk_header_row\nwait_frames 3\nclick_ui "context_menu_item_Show 20 lines {direction}"\n' + settle()

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:b.cpp\nright_click_ui hunk_header_row\nwait_frames 3\nclick_ui "context_menu_item_Comment on hunk"\nwait_frames 3\nclick_ui comment_input\ntype "Keep this folded"\nclick_ui comment_add_btn\nclick_ui jump_to_diff:a.cpp\n' + capture('initial', 1)
    script += expand('below') + capture('below', 1)
    script += expand('above') + capture('above', 1)
    script += 'key J\n' + capture('after_jump', 1)
    script += 'key K\n' + capture('after_back', 1)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 20000\n' + capture('expanded_top', 1)
    script += 'click_text "Split"\n' + capture('split', 1)
    script += 'click_text "Unified"\n' + settle()
    script += 'right_click_ui jump_to_diff:a.cpp\nwait_frames 3\nclick_ui "context_menu_item_Open source"\n' + capture('source', 2)
    script += 'click_ui full_file_back\n' + capture('returned', 2)
    script += 'click_ui jump_to_diff:b.cpp\nexpect_text "commented · click to expand"\nclick_ui jump_to_diff:a.cpp\n' + settle()
    script += 'right_click_ui hunk_header_row\nwait_frames 3\nclick_ui "context_menu_item_Show 20 lines below"\nwait_frames 1\n'
    script += 'click_text "Local context fixture"\nwait_for_refresh\nkey ENTER\nclick_ui jump_to_diff:a.cpp\n' + capture('commit', 2)
    script += expand('below') + capture('commit_below', 2)
    script += 'native_menu_action "Compare Revisions..."\nwait_frames 3\n'
    script += f'click_ui compare_base\nkey CMD+A\ntype "{base}"\nclick_ui compare_target\nkey CMD+A\ntype "{target}"\nclick_ui compare_submit\nwait_for_refresh\nwait_frames 20\n'
    script += 'click_ui jump_to_diff:a.cpp\n' + capture('comparison', 3)
    script += expand('below') + capture('comparison_below', 3)
    script += 'resize 1800 3000\nwait_frames 10\nhover_ui diff_scroll\nscroll_wheel 0 20000\nwait_frames 15\nright_click_text "@@ -57,7"\nwait_frames 3\nclick_ui "context_menu_item_Show 20 lines above"\n' + capture('overlap', 3)
    script += 'click_ui jump_to_diff:c.cpp\n' + settle() + expand('below') + capture('limited', 3)
    script += 'right_click_text "@@ -57,7"\nwait_frames 3\nclick_ui "context_menu_item_Show 20 lines above"\n' + capture('after_limit', 3)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
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
    def active(name):
        value = state(name)
        return next(t for t in value['tabs'] if t['id'] == value['active'])
    def rows(name):
        return [r for r in layout(name)['reading_rows'] if r['path'] == 'a.cpp']
    for initial, expanded in [('initial', 'below'), ('commit', 'commit_below'), ('comparison', 'comparison_below')]:
        assert not active(initial)['context_lines']
        assert list(active(expanded)['context_lines'].values()) == [20]
        lines = {r['line'] for r in rows(expanded)}
        assert 34 in lines and 54 not in lines, (zoom, expanded, lines)
        ranges = state(expanded)['hunk_context']['ranges']
        assert [(r['new_start'], r['count'], r['error']) for r in ranges] == [(34, 20, '')], (zoom, expanded, ranges)
        assert all('a_' in r['text'] for r in rows(expanded))
        assert not any(n.get('name') == 'hunk_context_notice' for n in layout(expanded)['nodes'])
        assert active(initial)['revision'] == active(expanded)['revision']
    for name in ['below', 'above']:
        focus = [n.get('focus_target', {}).get('region') for n in layout(name)['nodes'] if n['focused']]
        assert focus == ['Code'], (zoom, name, focus)
    for name in ['above']:
        first = next(r for r in rows('below') if r['line'] == 30 and r['sign'] == '+')
        retained = next(r for r in rows(name) if r['line'] == 30 and r['sign'] == '+')
        assert abs(first['rect']['y'] - retained['rect']['y']) < 1, (zoom, name, first, retained)
    jump = state('after_jump')
    assert jump['history'][jump['history_index']]['anchor']['line'] == 60, (zoom, jump['history'])
    assert active('above')['context_lines'] == active('returned')['context_lines']
    assert sorted(active('above')['context_lines'].values()) == [20, 20]
    assert 7 in {r['line'] for r in rows('expanded_top')}, (zoom, rows('expanded_top'))
    assert {r['side'] for r in rows('split') if r['line'] == 7} == {1, 2}
    assert state('source')['hunk_context']['bytes'] == 0
    assert state('returned')['hunk_context']['lines'] == 40
    for name in ['below', 'above', 'returned']:
        assert state(name)['hunk_context']['bytes'] <= 256 * 1024
    for name in ['below', 'above', 'returned']:
        lines = [(r['line'], r['sign'], r['offset']) for r in rows(name)]
        assert len(lines) == len(set(lines)), (zoom, name)
    overlap = state('overlap')['hunk_context']
    assert overlap['lines'] == 23, (zoom, overlap)
    assert sorted((r['new_start'], r['count']) for r in overlap['ranges']) == [(34, 20), (54, 3)]
    overlap_rows = [(r['line'], r['sign'], r['offset']) for r in rows('overlap')]
    assert len(overlap_rows) == len(set(overlap_rows)), (zoom, overlap_rows)
    limited = [r for r in state('limited')['hunk_context']['ranges'] if r['key'].startswith('c.cpp') ]
    assert len(limited) == 1 and limited[0]['error'] and limited[0]['count'] == 0, (zoom, limited)
    after_limit = [r for r in state('after_limit')['hunk_context']['ranges'] if r['key'].startswith('c.cpp') and not r['error']]
    assert [(r['new_start'], r['count']) for r in after_limit] == [(37, 20)], (zoom, after_limit)
    assert git('diff') == before and git('diff', '--cached') == ''
    print(f'PASS {zoom}% local context, working/commit/comparison, 20-line ranges and source return', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
