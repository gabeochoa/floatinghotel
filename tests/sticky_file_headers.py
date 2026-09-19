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

def git(*arguments):
    return subprocess.check_output(['git', '-C', str(repo), *arguments], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Sticky header fixture'),
                ('config', 'user.email', 'header@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
for name in ['a', 'b', 'c']:
    (repo / f'{name}.cpp').write_text(''.join(f'int {name}_{i} = {i}; // café\n' for i in range(1, 241)))
git('add', '.')
git('commit', '-qm', 'Fold the pinned header')
(repo / 'working.cpp').write_text(''.join(f'int working_{i} = {i};\n' for i in range(1, 241)))
original_status = git('status', '--porcelain')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
results = []
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Fold the pinned header"\nwait_for_refresh\nkey ENTER\nwait_frames 20\nscreenshot flow\n'
    script += 'hover_ui commit_detail_scroll\nscroll_wheel 0 -24\nwait_frames 30\nscreenshot pinned\n'
    script += 'key CMD+F\nwait_frames 5\nclick_ui diff_find_input\ntype "a_80"\nwait_frames 20\nscreenshot found\nkey ESCAPE\nwait_frames 5\nclick_ui review_display_mode\nwait_frames 3\nclick_ui "context_menu_item_All files"\nwait_frames 10\nhover_ui commit_detail_scroll\nscroll_wheel 0 100000\nwait_frames 30\nscroll_wheel 0 -24\nwait_frames 30\n'
    script += f'hover_ui commit_detail_scroll\nscroll_wheel 0 -{340 * zoom / 100}\nwait_frames 30\nscreenshot crossing\nscroll_wheel 0 100000\nwait_frames 30\nscroll_wheel 0 -24\nwait_frames 30\n'
    script += 'click_ui fold_file:a.cpp\nwait_frames 25\nscreenshot folded\n'
    script += 'click_ui fold_file:a.cpp\nwait_frames 25\nscreenshot unfolded\n'
    script += 'hover_ui commit_detail_scroll\nscroll_wheel 0 -24\nwait_frames 30\nclick_ui approve_file_btn\nwait_frames 8\nscreenshot viewed\n'
    script += 'click_ui open_full_file\nwait_for_refresh\nwait_frames 15\nscreenshot source\n'
    script += 'click_ui full_file_back\nwait_for_refresh\nwait_frames 20\nscreenshot returned\n'
    script += 'click_ui jump_to_diff:b.cpp\nwait_frames 20\nhover_ui commit_detail_scroll\nscroll_wheel 0 -24\nwait_frames 30\nscreenshot second\n'
    script += 'click_ui fold_file:b.cpp\nwait_frames 25\nscreenshot second_folded\n'
    script += 'click_ui fold_file:b.cpp\nwait_frames 25\nresize 1250 900\nwait_frames 30\nhover_ui commit_detail_scroll\nscroll_wheel 0 -24\nwait_frames 30\nscreenshot narrow\n'
    script += 'click_ui fold_file:b.cpp\nwait_frames 25\nscreenshot narrow_folded\n'
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nwait_frames 20\nhover_ui diff_scroll\nscroll_wheel 0 -24\nwait_frames 30\nscreenshot working_pinned\nclick_ui fold_file:working.cpp\nwait_frames 25\nscreenshot working_folded\n'
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def snapshot(name):
        return json.loads((directory / f'{name}.json').read_text())
    def header(name, file):
        data = snapshot(name)
        nodes = {n['id']: n for n in data['nodes']}
        fold = next(n for n in nodes.values() if n.get('name') == f'fold_file:{file}')
        return nodes[nodes[fold['parent']]['parent']], fold, nodes
    first, fold, nodes = header('flow', 'a.cpp')
    pinned, pinned_fold, pinned_nodes = header('pinned', 'a.cpp')
    assert first['id'] == pinned['id'] and fold['id'] == pinned_fold['id'], (zoom, 'Header identity changed')
    assert pinned['sticky_offset'] > 0, (zoom, pinned)
    assert first['rect']['height'] == pinned['rect']['height']
    assert not any(n.get('name') == 'sticky_diff_context' for n in pinned_nodes.values())
    def descendants(root, values):
        return [values[i] for i in root['children']] + [n for i in root['children'] for n in descendants(values[i], values)]
    assert [(n.get('name'), n.get('text')) for n in descendants(first, nodes)] == [(n.get('name'), n.get('text')) for n in descendants(pinned, pinned_nodes)]
    for name, file in [('pinned', 'a.cpp'), ('returned', 'a.cpp'), ('second', 'b.cpp'), ('narrow', 'b.cpp')]:
        value, control, values = header(name, file)
        viewport = next(n for n in values.values() if n.get('name') == 'commit_detail_scroll')
        assert value['sticky_offset'] > 0, (zoom, name, value)
        assert abs(value['rect']['y'] - viewport['visible_rect']['y']) < 1, (zoom, name, value, viewport)
        assert control['visible_rect']['height'] >= control['rect']['height'] - 1, (zoom, name, control)
        assert len([n for n in values.values() if n.get('name') == f'fold_file:{file}']) == 1
    for name, file in [('folded', 'a.cpp'), ('second_folded', 'b.cpp'), ('narrow_folded', 'b.cpp')]:
        assert not any(r['path'] == file for r in snapshot(name)['reading_rows']), (zoom, name)
        value, control, _ = header(name, file)
        assert control['visible_rect']['height'] > 0, (zoom, name, control)
    assert any(r['path'] == 'a.cpp' for r in snapshot('unfolded')['reading_rows'])
    assert any(n.get('name') == 'viewed_check' for n in snapshot('viewed')['nodes'])
    assert any(n.get('name') == 'full_file_back' for n in snapshot('source')['nodes'])
    assert all(r['path'] == 'a.cpp' for r in snapshot('source')['reading_rows'])
    found, control, values = header('found', 'a.cpp')
    match = next(n for n in values.values() if n.get('name') == 'diff_find_match' and n['rendered'])
    assert match['rect']['y'] >= found['rect']['y'] + found['rect']['height'] - 1, (zoom, 'Find hidden by header', match['rect'], found['rect'])
    crossed, control, values = header('crossing', 'b.cpp')
    viewport = next(n for n in values.values() if n.get('name') == 'commit_detail_scroll')
    assert crossed['sticky_offset'] > 0 and abs(crossed['rect']['y'] - viewport['rect']['y']) < 1, (zoom, 'crossing', crossed)
    assert not any(n.get('name') == 'fold_file:a.cpp' and n['visible_rect']['height'] > 0 for n in values.values())
    working, control, values = header('working_pinned', 'working.cpp')
    viewport = next(n for n in values.values() if n.get('name') == 'diff_scroll')
    assert working['sticky_offset'] > 0 and abs(working['rect']['y'] - viewport['rect']['y']) < 1, (zoom, 'working', working)
    assert not any(r['path'] == 'working.cpp' for r in snapshot('working_folded')['reading_rows'])
    assert git('status', '--porcelain') == original_status and git('diff', '--cached') == ''
    results.append({'zoom': zoom, 'header_id': pinned['id'], 'fold_id': pinned_fold['id'], 'passed': True})
    print(f'PASS {zoom}% same header and fold target, pinned actions, source return, second file and narrow window', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps({'passed': True, 'binary_sha256': digest, 'results': results}, indent=2) + '\n')
