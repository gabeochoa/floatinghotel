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
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Selected fixture'),
                ('config', 'user.email', 'selected@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
for name in ['a', 'b', 'c']:
    (repo / f'{name}.cpp').write_text(f'int {name}_value = 1;\n')
git('add', '.')
git('commit', '-qm', 'Selected file fixture')
for name in ['a', 'b', 'c']:
    (repo / f'{name}.cpp').write_text(f'int {name}_value = 2;\n')
git('add', 'c.cpp')
(repo / 'c.cpp').write_text('int c_value = 3;\n')
other = out / 'other_repository'
subprocess.run(['git', 'clone', '-q', str(repo), str(other)], check=True)
before = (git('diff'), git('diff', '--cached'))
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def capture(name, count=2):
    return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'

def mode(value):
    return 'click_ui review_display_mode\nwait_frames 3\nclick_ui "context_menu_item_' + value + '"\nwait_frames 10\n'

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'
    settings.mkdir()
    setup = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script = setup + 'click_text "Selected file fixture"\nwait_for_refresh\nkey ENTER\n'
    script += 'click_ui jump_to_diff:b.cpp\n' + capture('selected')
    if not args.baseline:
        script += 'click_ui approve_file_btn\n' + capture('viewed')
        script += 'click_ui diff_options_toggle\n' + capture('progress')
        script += 'expect_text "1/3 files reviewed"\nclick_ui diff_options_toggle\n'
        script += 'click_text "Open file"\nwait_for_refresh\n' + capture('source', 3)
        script += 'click_ui full_file_back\n' + capture('returned', 3)
        script += mode('All files') + capture('all', 3)
        script += 'click_ui jump_to_diff:c.cpp\n' + capture('all_selected', 3)
        script += mode('Selected file') + capture('single_c', 3)
        script += 'resize 1150 850\n' + capture('narrow', 3)
        script += 'click_ui open_tabs_menu\nwait_frames 3\nclick_ui "context_menu_item_Unstaged changes"\nwait_for_refresh\n'
        script += 'click_ui jump_to_diff:b.cpp\n' + capture('working', 3)
        script += 'click_ui comment_hunk_btn\nwait_frames 4\nclick_ui comment_input\ntype "Feedback on selected b"\nclick_ui comment_add_btn\nwait_frames 8\nkey ESCAPE\n' + capture('feedback', 3)
        script += mode('All files') + capture('working_all', 3)
        script += 'click_ui review_staged_changes\nwait_for_refresh\nclick_ui jump_to_diff:c.cpp\n' + capture('staged', 3)
        script += 'save_window_state\nbench_frames 120\nexpect_p99_below 20\n'
    def replay(name, script, repository=repo):
        target = directory / name
        target.mkdir()
        path = target / 'journey.e2e'
        path.write_text(script)
        with (target / 'run.log').open('w') as log:
            result = subprocess.run([str(binary), str(repository), '--test-mode', '--headless', f'--test-script={path}',
                f'--screenshot-dir={target}', '--e2e-timeout=180'], cwd=ROOT,
                env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings)), stdout=log, stderr=subprocess.STDOUT, timeout=210)
        assert result.returncode == 0, (zoom, target / 'run.log')
        return target
    run = replay('journey', script)
    def layout(name):
        return json.loads((run / f'{name}.json').read_text())
    def paths(name):
        return {n['focus_target']['item'] for n in layout(name)['nodes'] if n.get('name') == 'file_header_row' and n['rendered']}
    def node(name, debug):
        return next(n for n in layout(name)['nodes'] if n.get('name') == debug and n['rendered'])
    if args.baseline:
        assert paths('selected') == {'a.cpp', 'b.cpp', 'c.cpp'}
    else:
        for name in ['selected', 'viewed', 'returned', 'working']:
            assert paths(name) == {'b.cpp'}, (zoom, name, paths(name))
        for name in ['all', 'all_selected', 'working_all']:
            assert paths(name) == {'a.cpp', 'b.cpp', 'c.cpp'}, (zoom, name, paths(name))
        for name in ['single_c', 'narrow']:
            assert paths(name) == {'c.cpp'}
        assert '1 of 3 files viewed' in node('viewed', 'diff_reading_end')['text']
        for name in ['feedback', 'working_all']:
            state = json.loads((run / f'{name}.workspace.json').read_text())
            assert state['review']['comments'] == 1
        for name in ['selected', 'narrow', 'working']:
            control = node(name, 'review_display_mode')['rect']
            assert control['height'] <= 24.1 * zoom / 100
            assert abs(node(name, 'review_display_mode')['visible_rect']['width'] - control['width']) < .1
        saved = json.loads((settings / 'settings.json').read_text())
        assert saved['review_display_modes'][str(repo)] == 'all'
        assert paths('staged') == {'c.cpp'}
        assert any(r['text'] == 'int c_value = 2;' for r in layout('staged')['reading_rows'])
        assert all(r['text'] != 'int c_value = 3;' for r in layout('staged')['reading_rows'])
        assert any(n.get('text') == 'Feedback 1' for n in layout('working_all')['nodes'])
        assert any(n.get('text') == 'Feedback 0' for n in layout('staged')['nodes'])
        again = replay('restart', setup + 'click_text "Selected file fixture"\nwait_for_refresh\nclick_ui jump_to_diff:a.cpp\nwait_frames 15\nscreenshot restored\n')
        j = json.loads((again / 'restored.json').read_text())
        assert len([n for n in j['nodes'] if n.get('name') == 'file_header_row' and n['rendered']]) == 3
        independent = replay('other_repo', setup + 'click_text "Selected file fixture"\nwait_for_refresh\nclick_ui jump_to_diff:b.cpp\nwait_frames 15\nscreenshot other\n', other)
        j = json.loads((independent / 'other.json').read_text())
        headers = [n for n in j['nodes'] if n.get('name') == 'file_header_row' and n['rendered']]
        assert len(headers) == 1 and headers[0]['focus_target']['item'] == 'b.cpp'
    assert before == (git('diff'), git('diff', '--cached'))
    print(f'PASS {zoom}% selected-file review' + (' baseline' if args.baseline else ', whole-review progress, source return, modes, narrow geometry and restart'), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest), indent=2) + '\n')
