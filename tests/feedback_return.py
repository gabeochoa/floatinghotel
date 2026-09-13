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
parser.add_argument('--zooms', nargs='+', type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def capture(name):
    return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 10\nworkspace_checkpoint 1 {name}\nscreenshot {name}\n'

for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    repo = directory / 'fixture'
    repo.mkdir()
    settings = directory / 'settings'
    settings.mkdir()

    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args], text=True)

    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Feedback fixture'),
                    ('config', 'user.email', 'feedback@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*command)
    code = repo / 'code.cpp'
    code.write_text(''.join(f'int value_{i:03} = 0;\n' for i in range(1, 81)))
    git('add', '.')
    git('commit', '-qm', 'Feedback fixture')
    code.write_text(''.join(f'int value_{i:03} = {i if 5 <= i <= 7 else 0};\n' for i in range(1, 81)))
    before = git('diff')
    script = 'resize 1800 1400\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:code.cpp\nwait_frames 10\n'
    script += 'focus_ui diff_scroll\nkey CTRL+G\nwait_frames 4\nclick_ui line_picker_input\nkey CMD+A\ntype "5:3"\nkey ENTER\nwait_frames 10\nfocus_ui diff_scroll\nkey SHIFT+DOWN\n'
    script += capture('selected')
    script += 'click_ui comment_hunk_btn\nwait_frames 4\ntype "Keep this selected range visible"\n' + capture('composing')
    script += 'key ESCAPE\n' + capture('dismissed')
    script += 'click_ui comment_hunk_btn\n' + capture('reopened')
    script += 'click_ui comment_add_btn\n' + capture('saved')
    script += 'key RIGHT\nkey RIGHT\n' + capture('reading')
    script += 'right_click_ui hunk_header_row\nwait_frames 4\nclick_ui "context_menu_item_Fold hunk"\n' + capture('folded')
    script += 'click_ui hunk_folded_marker\n' + capture('unfolded')
    script += 'click_ui basket_item_resolve\n' + capture('resolved')
    script += 'key ESCAPE\nhover_ui diff_scroll\nscroll_wheel 0 20000\nwait_frames 12\nclick_ui approve_file_btn\n' + capture('reviewed')
    script += 'save_window_state\nbench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    env = dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings), FH_TEST_PERSIST_REVIEW='1')
    if not args.snapshots:
        env['FH_TEST_NATIVE_HIDDEN'] = '1'
    command = [str(binary), str(repo), '--test-mode', f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180']
    if args.snapshots:
        command.append('--headless')
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'

    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())

    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())

    def tab(name):
        value = state(name)
        return next(t for t in value['tabs'] if t['id'] == value['active'])

    selection = tab('selected')['selection']
    assert selection['anchor_line'] == 5 and selection['head_line'] == 6, selection
    for name in ['composing', 'reopened']:
        review = state(name)['review']
        assert review['draft'] == 'Keep this selected range visible', (zoom, name, review)
        assert review['composing_line'] == 5 and review['composing_end_line'] == 6 and not review['composing_old_side'], review
        controls = [n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') in ['comment_input', 'comment_add_btn']]
        assert len(controls) == 2
        assert all(n['visible_rect']['height'] >= n['rect']['height'] - .1 for n in controls), (zoom, name, controls)
    assert state('dismissed')['review']['composing'] == ''
    for name in ['dismissed', 'reopened', 'saved']:
        assert tab(name)['selection'] == selection, (zoom, name, tab(name))
    assert state('saved')['review']['comments'] == 1 and state('saved')['review']['folded_hunks'] == 0
    assert tab('reading')['caret'] != tab('saved')['caret'], (zoom, 'keyboard did not return to code')
    assert state('folded')['review']['folded_hunks'] == 1
    assert state('unfolded')['review']['folded_hunks'] == 0
    for name in ['saved', 'reading', 'unfolded']:
        view = layout(name)
        nodes = {n['id']: n for n in view['nodes']}
        rows = [r for r in view['reading_rows'] if r['path'] == 'code.cpp' and r['line'] in [5, 6] and r['sign'] == '+']
        assert rows and all(nodes[r['id']]['rendered'] and nodes[r['id']]['visible_rect']['height'] > 0 for r in rows), (zoom, name, rows)
    records = list((settings / 'reviews').glob('*.json'))
    assert len(records) == 1, records
    record = json.loads(records[0].read_text())
    comment = record['comments'][0]
    assert comment['line'] == 5 and comment['end_line'] == 6 and comment['resolved'], comment
    assert comment['text'] == 'Keep this selected range visible' and comment['code_context'], comment
    assert git('diff', '--cached') == '' and git('diff') == before
    print(f'PASS {zoom}% selected range, composer autofocus, draft restoration, return to code, explicit folding and unchanged Git', flush=True)

assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
