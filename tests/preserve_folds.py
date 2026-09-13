import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--zooms', type=int, nargs='+', default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
for stem in ['a', 'b']:
    (repo / f'{stem}.cpp').write_text(''.join(f'int {stem}_value_{i} = {i};\n' for i in range(20)))
def git(*args): return subprocess.check_output(['git', '-C', str(repo), *args])
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Fold fixture'),
                ('config', 'user.email', 'fold@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Fold fixture')]: git(*command)
for stem in ['a', 'b']:
    p = repo / f'{stem}.cpp'
    p.write_text(p.read_text().replace('value_10 = 10', 'value_10 = 1000'))
initial_diff = git('diff')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
geometry = {}
for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count=1): return f'wait_frames 12\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    def select(stem): return f'click_ui jump_to_diff:{stem}.cpp\nwait_frames 8\n'
    def comment(stem): return select(stem) + f'click_ui comment_hunk_btn\nwait_frames 4\nclick_ui comment_input\ntype "Feedback on {stem}"\nclick_ui comment_add_btn\nwait_frames 8\nkey ESCAPE\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\n'
    script += comment('a') + capture('a_folded')
    script += 'click_ui fold_file:a.cpp\n' + comment('b') + capture('b_folded')
    script += select('a') + 'click_ui fold_file:a.cpp\n' + select('b') + select('a') + capture('a_reselected')
    script += 'click_text "Open file"\nwait_for_refresh\n' + capture('source', 2)
    script += 'click_ui full_file_back\nwait_for_refresh\n' + capture('returned', 2)
    script += 'key ALT+LEFT\nwait_for_refresh\nkey ALT+RIGHT\nwait_for_refresh\n' + capture('history_return', 2)
    script += select('a') + 'hover_ui diff_scroll\nscroll_wheel 0 200\nwait_frames 8\nclick_ui fold_file:a.cpp\n' + capture('file_folded', 2)
    script += select('b') + select('a') + capture('file_reselected', 2)
    script += 'click_ui fold_file:a.cpp\n' + capture('file_expanded', 2)
    script += 'click_ui content_document_1\nkey CMD+F\nwait_frames 4\ntype "a_value_10"\n' + capture('find_reveal', 2)
    script += 'key ESCAPE\n' + select('b') + capture('other_still_folded', 2)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def layout(name): return json.loads((directory / f'{name}.json').read_text())
    def state(name): return json.loads((directory / f'{name}.workspace.json').read_text())
    def markers(name): return [n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') == 'hunk_folded_marker' and n['visible_rect']['height'] > 0]
    def rows(name, path): return [r for r in layout(name)['reading_rows'] if r['path'] == path and r['rect']['height'] > 0]
    for name in ['a_folded', 'b_folded', 'a_reselected', 'returned', 'history_return', 'file_expanded', 'other_still_folded']:
        assert markers(name), (zoom, name, 'folded hunk missing')
    assert not rows('b_folded', 'b.cpp'), (zoom, 'second file hunk did not fold')
    assert not rows('a_reselected', 'a.cpp'), (zoom, 'first file hunk did not stay folded')
    assert not rows('other_still_folded', 'b.cpp'), (zoom, 'Find expanded the other file')
    assert not rows('file_reselected', 'a.cpp'), (zoom, 'selected file was expanded')
    assert not rows('file_folded', 'a.cpp'), (zoom, 'file did not fold')
    assert state('returned')['review']['comments'] == state('other_still_folded')['review']['comments'] == 2
    assert any(r['path'] == 'a.cpp' and r['line'] == 11 for r in layout('find_reveal')['reading_rows']), (zoom, 'Find did not reveal the matching hunk')
    geometry[str(zoom)] = {name: [n['visible_rect'] for n in markers(name)] for name in ['a_reselected', 'returned', 'other_still_folded']}
    assert git('diff', '--cached') == b'' and git('diff') == initial_diff
    print(f'PASS {zoom}%: file selection, source return, history and Find preserve unrelated folds', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=geometry), indent=2) + '\n')
