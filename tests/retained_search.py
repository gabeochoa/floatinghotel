import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
for name in ['a.cpp', 'b.cpp', 'gone.cpp']:
    (repo / name).write_text(''.join(f'int NEEDLE_{i:02d} = {i};\n' for i in range(1, 81)))
(repo / 'gone.cpp').write_text((repo / 'gone.cpp').read_text().replace('NEEDLE_05', 'NEEDLE_05_GONE'))
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Search fixture'),
             ('config', 'user.email', 'search@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Original search files')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
original = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
subprocess.run(['git', '-C', str(repo), 'rm', 'gone.cpp'], check=True, capture_output=True)
subprocess.run(['git', '-C', str(repo), 'commit', '-qm', 'Remove historical file'], check=True)
(repo / 'a.cpp').write_text((repo / 'a.cpp').read_text().replace('NEEDLE_01', 'WORKING_ONLY'))
status_before = subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain'])
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count=3):
        return f'wait_for_refresh\nwait_frames 20\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Original search files"\nwait_for_refresh\nkey ENTER\n' + capture('review', 2)
    script += 'key CMD+SHIFT+F\nwait_frames 3\n' + capture('opened', 2)
    script += 'click_ui repo_search_input\ntype "NEEDLE"\nclick_ui repo_search_submit\n' + capture('results', 2)
    script += 'hover_ui repo_search_results\nscroll_wheel 0 -14\n' + capture('scrolled', 2)
    script += 'click_ui repo_search_result\n' + capture('source')
    script += 'hover_ui repo_search_results\nscroll_wheel 0 -8\n' + capture('moved')
    script += 'click_ui repo_search_result\n' + capture('second_source')
    script += 'click_ui repo_search_close\n' + capture('closed')
    script += 'key CMD+SHIFT+F\n' + capture('reopened')
    script += 'click_ui open_tabs_menu\nwait_frames 3\nclick_ui "context_menu_item_Unstaged changes"\n' + capture('working_tab')
    script += 'click_ui repo_search_input\nkey CMD+A\ntype "NEEDLE_05"\nkey ENTER\n' + capture('scoped_results')
    script += 'click_text "NEEDLE_05_GONE"\n' + capture('historical_source')
    script += 'click_ui full_file_back\n' + capture('origin')
    script += 'key ESCAPE\n' + capture('escaped')
    script += 'key CMD+SHIFT+F\n' + capture('again')
    script += 'click_ui open_tabs_menu\nwait_frames 3\nclick_ui "context_menu_item_Unstaged changes"\nclick_ui repo_search_options\nwait_frames 3\nhover_ui repo_search_options_scroll\nscroll_wheel 0 -12\nwait_frames 20\nclick_ui repo_search_current_scope\nwait_for_refresh\nclick_ui repo_search_options\n' + capture('working_scope')
    script += 'click_ui repo_search_result\nresize 1100 800\n' + capture('narrow')
    script += 'new_tab\nkey CMD+SHIFT+F\nwait_frames 10\nworkspace_checkpoint 1 other_repository\nscreenshot other_repository\nclose_tab\n' + capture('returned_repository')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def workspace(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def active(name):
        w = workspace(name)
        return next(t for t in w['tabs'] if t['id'] == w['active'])
    def nodes(name):
        return [n for n in layout(name)['nodes'] if n['rendered'] and not n['hidden']]
    def search(name):
        return workspace(name)['search']
    assert workspace('review')['active'] == workspace('opened')['active']
    assert workspace('review')['history'] == workspace('opened')['history']
    assert search('results')['matches'] == 240 and search('results')['revision'] == original
    for name in ['source', 'second_source', 'reopened', 'working_tab']:
        assert search(name)['open'] and search(name)['query'] == 'NEEDLE' and search(name)['submissions'] == 1
        assert search(name)['matches'] == 240 and search(name)['revision'] == original
    assert search('scrolled')['scroll'] > 0
    assert abs(search('source')['scroll'] - search('scrolled')['scroll']) < .5
    for name in ['closed', 'reopened']:
        assert search(name)['selected'] == search('second_source')['selected']
        assert abs(search(name)['scroll'] - search('second_source')['scroll']) < .5
        assert active(name) == active('second_source')
    assert not search('closed')['open']
    assert search('scoped_results')['matches'] == 3 and search('scoped_results')['revision'] == original
    assert active('historical_source')['path'] == 'gone.cpp' and active('historical_source')['revision'] == original
    assert active('origin')['id'] == active('review')['id']
    assert not search('escaped')['open'] and active('escaped')['id'] == active('origin')['id']
    assert search('again')['submissions'] == 2 and search('again')['matches'] == 3
    assert search('working_scope')['submissions'] == 3 and search('working_scope')['revision'] == '' and search('working_scope')['matches'] == 2
    assert not workspace('other_repository')['search']['open']
    assert search('returned_repository') == search('narrow')
    for name in ['opened', 'results', 'scrolled', 'source', 'second_source', 'reopened', 'historical_source', 'narrow']:
        rendered = nodes(name)
        pane = next(n for n in rendered if n.get('name') == 'repo_search_pane')['visible_rect']
        reader = next(n for n in rendered if n.get('name') in ['diff_scroll', 'commit_detail_scroll'])['visible_rect']
        assert pane['width'] > 100 and pane['height'] > 100 and reader['width'] > 200 and reader['height'] > 100, (zoom, name, pane, reader)
        assert pane['x'] + pane['width'] <= reader['x'] + .5, (zoom, name)
        for control in ['repo_search_input', 'repo_search_submit', 'repo_search_close', 'repo_search_results']:
            node = next(n for n in rendered if n.get('name') == control)
            assert node['visible_rect']['height'] >= node['rect']['height'] - 1, (zoom, name, control, node)
        assert sum(n.get('name') == 'repo_search_result' for n in rendered) < 50, (zoom, name)
    assert any(n.get('focus_target', {}).get('region') == 'DocumentTabs' and n['focused'] for n in nodes('source')), zoom
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == status_before
    print(f'PASS {zoom}%: reader retained, scoped results/selection/scroll retained, no resubmission on reopen, deleted historical file, explicit working scope, narrow geometry and repository isolation', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, original_revision=original), indent=2) + '\n')
