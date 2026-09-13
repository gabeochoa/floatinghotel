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
def capture(name, count=1):
    return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 10\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    repo = directory / 'fixture'
    repo.mkdir()
    settings = directory / 'settings'
    settings.mkdir()
    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Baseline fixture'),
                    ('config', 'user.email', 'baseline@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*command)
    source = repo / 'code.cpp'
    original = ''.join(f'int value_{i:03} = {i};\n' for i in range(1, 81))
    source.write_text(original)
    git('add', '.')
    git('commit', '-qm', 'Baseline fixture')
    head = git('rev-parse', 'HEAD')
    source.write_text(original.replace('value_017 = 17', 'value_017 = 170'))
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
        return run
    def state(run, name):
        return json.loads((run / f'{name}.workspace.json').read_text())
    def layout(run, name):
        return json.loads((run / f'{name}.json').read_text())
    def nodes(run, name):
        return {n['name']: n for n in layout(run, name)['nodes'] if n.get('name') and n['rendered']}
    initial = replay('initial', 'click_ui review_unstaged_changes\n' + capture('opened') + 'save_window_state\n')
    baseline = repo / '.git/floatinghotel-baseline.cbor'
    assert not baseline.exists() and not state(initial, 'opened')['review']['baseline']
    saved = replay('capture', 'click_ui review_unstaged_changes\nclick_ui save_review_baseline\n' + capture('saved') + 'save_window_state\n')
    baseline_bytes = baseline.read_bytes()
    metadata = state(saved, 'saved')['review']
    assert metadata['baseline_head'] == head and metadata['baseline_captured_at'] > 0
    assert head[:8] in nodes(saved, 'saved')['review_baseline_identity']['text']
    source.write_text(original.replace('value_017 = 17', 'value_017 = 171'))
    script = 'click_text "Baseline fixture"\nwait_for_refresh\nkey ENTER\nkey CMD+P\nwait_frames 3\ntype "code.cpp"\nwait_for_refresh\nkey ENTER\nwait_for_refresh\n'
    script += 'key CTRL+G\nwait_frames 3\ntype "40:5"\nkey ENTER\nwait_for_refresh\nfocus_ui diff_scroll\nkey SHIFT+RIGHT\n' + capture('before', 3)
    script += 'native_menu_action "Since last review"\n' + capture('delta', 3)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -15\nwait_frames 8\nkey ESCAPE\n' + capture('returned', 3)
    script += 'native_menu_action "Since last review"\n' + capture('again', 3)
    script += 'click_ui snapshot_close\n' + capture('closed', 3)
    script += 'click_ui review_unstaged_changes\n' + capture('working', 3)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    delta = replay('return', script)
    assert baseline.read_bytes() == baseline_bytes
    for name in ['delta', 'again']:
        assert layout(delta, name)['reading_rows'] == [] and layout(delta, name)['reading_projections'] == [], (zoom, name, 'retained reader leaked into snapshot')
        rows = [n for n in layout(delta, name)['nodes'] if n.get('name') == 'diff_line' and n['rendered']]
        assert any('- int value_017 = 170;' in r['text'] for r in rows), (zoom, name)
        assert any('+ int value_017 = 171;' in r['text'] for r in rows), (zoom, name)
        assert state(delta, name)['review']['baseline_captured_at'] == metadata['baseline_captured_at']
        assert 'UTC' in nodes(delta, name)['snapshot_baseline_identity']['text']
    before = state(delta, 'before')
    for name in ['returned', 'closed']:
        after = state(delta, name)
        assert before['active'] == after['active'] and before['tabs'] == after['tabs'] and before['history_index'] == after['history_index'], (zoom, name)
        a = before['history'][before['history_index']]['anchor']
        b = after['history'][after['history_index']]['anchor']
        assert a == b, (zoom, name, a, b)
        view = layout(delta, name)
        viewport = nodes(delta, name)['diff_scroll']['rect']
        rows = [r for r in view['reading_rows'] if r['line'] == a['line'] and r['path'] == a['path']]
        assert rows and min(abs(r['rect']['y'] - viewport['y'] - viewport['height'] * a['fraction']) for r in rows) < 2, (zoom, name, a, rows)
    missing_path = baseline.with_suffix('.missing')
    baseline.rename(missing_path)
    missing = replay('missing', 'native_menu_action "Since last review"\n' + capture('missing') + 'expect_text "Review snapshot is missing or too large. Capture a new baseline."\n')
    assert not baseline.exists()
    assert state(missing, 'missing')['review']['baseline'] == str(baseline)
    missing_path.rename(baseline)
    replaced = replay('replace', 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui review_baseline_actions\nwait_frames 3\nclick_ui "context_menu_item_Replace saved baseline"\n' + capture('replaced', 1) + 'click_ui since_last_review\n' + capture('unchanged', 1) + 'expect_text "No changes since saved review"\n')
    assert baseline.read_bytes() != baseline_bytes
    assert state(replaced, 'replaced')['review']['baseline_captured_at'] >= metadata['baseline_captured_at']
    assert git('diff', '--cached') == ''
    assert source.read_text() == original.replace('value_017 = 17', 'value_017 = 171')
    for run, name, debug in [(saved, 'saved', 'review_baseline_identity'), (delta, 'delta', 'snapshot_baseline_identity')]:
        node = nodes(run, name)[debug]
        assert node['rect']['width'] > 0 and node['rect']['height'] <= 28.1 * zoom / 100
        assert abs(node['rect']['width'] - node['visible_rect']['width']) < 1
    print(f'PASS {zoom}% explicit capture, saved bytes, identity/time, source return, replacement and unchanged index', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
