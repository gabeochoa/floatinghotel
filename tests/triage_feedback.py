import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--zooms', nargs='+', type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    repo = directory / 'fixture'
    repo.mkdir()
    settings = directory / 'settings'
    settings.mkdir()
    def git(*arguments):
        return subprocess.check_output(['git', '-C', str(repo), *arguments], text=True).strip()
    for arguments in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Feedback fixture'),
                      ('config', 'user.email', 'reader@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*arguments)
    code = repo / 'code.cpp'
    code.write_text(''.join(f'int value_{i} = 0;\n' for i in range(1, 21)))
    git('add', '.')
    git('commit', '-qm', 'Feedback fixture')
    code.write_text(''.join(f'int value_{i} = {i if i in [3, 6] else 0};\n' for i in range(1, 21)))
    before = git('diff')
    setup = 'resize 1800 1400\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    setup += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:code.cpp\nwait_frames 15\n'
    def capture(name):
        return f'wait_for_refresh\nwait_frames 15\nworkspace_checkpoint 1 {name}\nscreenshot {name}\n'
    def run(name, script):
        path = directory / f'{name}.e2e'
        path.write_text(script)
        with (directory / f'{name}.log').open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
                f'--screenshot-dir={directory}', '--e2e-timeout=120'], cwd=ROOT,
                env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings), FH_TEST_PERSIST_REVIEW='1'),
                stdout=log, stderr=subprocess.STDOUT, timeout=150)
        assert result.returncode == 0, directory / f'{name}.log'
    run('measure', setup + capture('initial'))
    snapshot = json.loads((directory / 'initial.json').read_text())
    nodes = {n['id']: n for n in snapshot['nodes']}
    clicks = ''
    for line in [3, 6]:
        row = next(r for r in snapshot['reading_rows'] if r['path'] == 'code.cpp' and r['line'] == line and r['sign'] == '+')
        rect = nodes[row['id']]['visible_rect']
        assert rect['height'] > 0, (zoom, line, rect)
        x, y = rect['x'] + rect['width'] * .6, rect['y'] + rect['height'] * .5
        clicks += f'hold_key 343\ndrag_to {x} {y} {x} {y}\nrelease_key 343\nwait_frames 5\n'
    script = setup + clicks + capture('disjoint')
    script += 'click_ui comment_disjoint_lines\nwait_frames 8\ntype "Review both separate changes"\n' + capture('composing')
    script += 'key ESCAPE\nwait_frames 5\nclick_ui comment_hunk_btn\n' + capture('draft_restored')
    script += 'click_ui comment_add_btn\n' + capture('saved')
    script += 'save_window_state\nbench_frames 120\nexpect_p99_below 20\n'
    run('feedback', script)
    records = list((settings / 'reviews').glob('*.json'))
    assert len(records) == 1, records
    comment = json.loads(records[0].read_text())['comments'][0]
    assert comment['text'] == 'Review both separate changes', comment
    assert [(r['first'], r['last'], r['old_side']) for r in comment['ranges']] == [(3, 3, False), (6, 6, False)], comment
    initial = json.loads((directory / 'initial.workspace.json').read_text())
    selected = json.loads((directory / 'disjoint.workspace.json').read_text())
    assert initial['tabs'][0].get('selection') == selected['tabs'][0].get('selection')
    assert git('diff', '--cached') == '' and git('diff') == before
    print(f'PASS {zoom}% disjoint feedback, draft restoration, unchanged selection and Git', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps({'passed': True, 'binary_sha256': digest}, indent=2) + '\n')
