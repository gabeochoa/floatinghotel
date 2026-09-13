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
        return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Follow-up fixture'),
                    ('config', 'user.email', 'followup@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*command)
    for name in 'abcde':
        (repo / f'{name}.cpp').write_text(''.join(f'int {name}_{i:03} = 0;\n' for i in range(1, 21)))
    git('add', '.')
    git('commit', '-qm', 'Follow-up fixture')
    head = git('rev-parse', 'HEAD')
    for name in 'abcde':
        text = ''.join(f'int {name}_{i:03} = {i};\n' for i in range(1, 21))
        if name == 'd':
            text = text.replace('int d_006 = 6;', 'needle').replace('int d_007 = 7;', 'needle')
        (repo / f'{name}.cpp').write_text(text)
    setup = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    env = dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings), FH_TEST_PERSIST_REVIEW='1')
    if not args.snapshots:
        env['FH_TEST_NATIVE_HIDDEN'] = '1'
    def replay(name, script):
        run = directory / name
        run.mkdir()
        path = run / 'journey.e2e'
        path.write_text(setup + script)
        command = [str(binary), str(repo), '--test-mode', f'--test-script={path}', f'--screenshot-dir={run}', '--e2e-timeout=180']
        if args.snapshots:
            command.append('--headless')
        with (run / 'run.log').open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=210)
        assert result.returncode == 0, run / 'run.log'
        for line in (run / 'run.log').read_text().splitlines():
            if 'Review loaded from ' in line:
                assert line.split('Review loaded from ', 1)[1].startswith(str(settings)), line
        return run
    replay('capture', 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui save_review_baseline\n' + capture('saved') + 'save_window_state\n')
    records = list((settings / 'reviews').glob('*.json'))
    assert len(records) == 1, records
    record = records[0]
    data = json.loads(record.read_text())
    def comment(path, context, resolved=False):
        return dict(scope='wt', file=path, line=5, end_line=5, old_side=False, resolved=resolved,
                    text='Review ' + path, revision=head, code_context='5: ' + context + '\n', kind='Comment')
    data['comments'] = [comment('b.cpp', 'int b_005 = 5;'), comment('b.cpp', 'int b_005 = 5;'),
                        comment('c.cpp', 'int c_005 = 5;'), comment('d.cpp', 'needle'), comment('e.cpp', 'int e_005 = 5;', True)]
    record.write_text(json.dumps(data))
    for name in 'ac':
        p = repo / f'{name}.cpp'
        p.write_text(p.read_text().replace(f'int {name}_005 = 5;', f'int {name}_005 = 500;'))
    original_diff = git('diff')
    baseline = (repo / '.git/floatinghotel-baseline.cbor').read_bytes()
    open_menu = 'native_menu_action "Since last review"\nwait_for_refresh\nwait_frames 10\nclick_ui follow_up_review\n'
    labels = ['Changed · a.cpp', 'Unresolved · b.cpp:5 · 2 comments', 'Outdated · c.cpp:5 · changed', 'Ambiguous · d.cpp:5']
    script = open_menu + capture('itinerary')
    for name, label in zip(['changed', 'comment', 'outdated', 'ambiguous'], labels):
        script += f'click_ui "context_menu_item_{label}"\n' + capture(name)
        if name != 'ambiguous':
            script += open_menu + 'wait_frames 5\n'
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    run = replay('journey', script)
    def layout(name, target=run):
        return json.loads((target / f'{name}.json').read_text())
    menu = [n for n in layout('itinerary')['nodes'] if n.get('name', '').startswith('context_menu_item_') and n['rendered']]
    assert {n['name'].removeprefix('context_menu_item_') for n in menu} == set(labels), (zoom, menu)
    for n in menu:
        assert abs(n['rect']['width'] - n['visible_rect']['width']) < 1
    for name, path in [('changed', 'a.cpp'), ('comment', 'b.cpp'), ('outdated', 'c.cpp'), ('ambiguous', 'd.cpp')]:
        view = layout(name)
        headers = [n for n in view['nodes'] if n.get('name') == 'file_header_row' and n['rendered']]
        assert len(headers) == 1 and headers[0]['focus_target']['item'] == path, (zoom, name, headers)
    state = json.loads((run / 'comment.workspace.json').read_text())
    tab = next(t for t in state['tabs'] if t['id'] == state['active'])
    assert tab['caret']['line'] == 5 and tab['caret']['path'] == 'b.cpp', tab
    assert any('Outdated' in n.get('text', '') for n in layout('outdated')['nodes'] if n['rendered'])
    assert any('Ambiguous' in n.get('text', '') for n in layout('ambiguous')['nodes'] if n['rendered'])
    data = json.loads(record.read_text())
    data['basket_open'] = False
    for c in data['comments']:
        c['resolved'] = True
    for i in range(2000):
        data['comments'].append(comment(f'other/file_{i:04}.cpp', 'unloaded'))
    record.write_text(json.dumps(data))
    bulk = replay('bulk', open_menu + capture('large') + 'bench_frames 120\nexpect_p99_below 20\nkey UP\nwait_frames 10\nscreenshot end\nkey ESCAPE\nbench_frames 120\nexpect_p99_below 20\n')
    for name in ['large', 'end']:
        items = [n for n in layout(name, bulk)['nodes'] if n.get('name', '').startswith('context_menu_item_') and n['rendered']]
        assert 0 < len(items) < 50, (zoom, name, len(items))
    assert any('file_1999.cpp' in n.get('text', '') for n in layout('end', bulk)['nodes'] if n['rendered'])
    assert (repo / '.git/floatinghotel-baseline.cbor').read_bytes() == baseline
    assert git('diff') == original_diff and git('diff', '--cached') == ''
    print(f'PASS {zoom}% deduplicated itinerary, changed and unresolved locations, warning preservation, 2000-item virtualization and unchanged Git', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
