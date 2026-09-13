import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--snapshots', action='store_true')
parser.add_argument('--limits-only', action='store_true')
parser.add_argument('--zooms', nargs='+', type=int, default=[100, 140, 200])
parser.add_argument('--modes', nargs='+', default=['working', 'index', 'commit', 'unified', 'split_before', 'split_after'])
args = parser.parse_args()
guard = ROOT / 'output/clipboard_guard'
marker = os.environ.get('FH_TEST_CLIPBOARD_MARKER')
if not marker:
    subprocess.run(['swiftc', str(ROOT / 'tests/clipboard_guard.swift'), '-o', str(guard)], check=True)
    marker = 'FH48_' + uuid.uuid4().hex
    raise SystemExit(subprocess.run([str(guard), 'guard', marker, sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]).returncode)
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Selection fixture'),
                ('config', 'user.email', 'selection@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
versions = {name: ''.join(f'{marker} {name} éλ row_{i}  \r\n' for i in range(1, 5000)) + f'{marker} {name} final'
            for name in ['original', 'staged', 'working']}
(repo / 'range.txt').write_bytes(versions['original'].encode())
git('add', '.')
git('commit', '-qm', 'Range fixture base')
commit = git('rev-parse', 'HEAD')
(repo / 'range.txt').write_bytes(versions['staged'].encode())
git('add', '.')
(repo / 'range.txt').write_bytes(versions['working'].encode())
before, staged_before = git('diff'), git('diff', '--cached')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
def settle():
    return 'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\n'
def capture(label, count):
    return settle() + f'workspace_checkpoint {count} {label}\nscreenshot {label}\n'
def active(path):
    state = json.loads(path.read_text())
    return next(t for t in state['tabs'] if t['id'] == state['active'])
def run(directory, label, script):
    settings = directory / (label + '_settings')
    settings.mkdir()
    script_path = directory / (label + '.e2e')
    script_path.write_text(script)
    with (directory / (label + '.log')).open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', *(['--headless'] if args.baseline or args.snapshots else []),
            f'--test-script={script_path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_NATIVE_HIDDEN='1', FH_TEST_SETTINGS_DIR=str(settings)),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, (label, directory / (label + '.log'))
    if not args.baseline and not args.snapshots:
        assert 'Native test window hidden=1 key=0' in (directory / (label + '.log')).read_text()
def read_clipboard(directory, label):
    path = directory / (label + '.clipboard.json')
    subprocess.run([str(guard), 'read', marker, str(path)], check=True)
    return json.loads(path.read_text())['text']
for zoom in args.zooms:
    for mode in ([] if args.limits_only else args.modes):
        directory = out / f'{zoom}-{mode}'
        directory.mkdir()
        source = mode in ['working', 'index', 'commit']
        count = (2 if mode == 'working' else 3) if source else 1
        setup = 'resize 1800 1400\nwait_frames 15\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        setup += 'screenshot zoom_ready\nclick_ui review_unstaged_changes\n' + capture('review_ready', 1)
        if mode == 'index': setup += 'click_ui review_staged_changes\n' + capture('scope_ready', 2)
        if mode == 'commit': setup += 'click_text "Range fixture base"\n' + capture('scope_ready', 2)
        if source:
            setup += 'key CMD+P\n' + settle() + 'screenshot picker_ready\ntype "range.txt"\nkey ENTER\n'
        else:
            setup += 'click_ui jump_to_diff:range.txt\n'
            if mode.startswith('split'): setup += 'click_text "Split"\n'
            setup += capture('diff_ready', count) + 'focus_ui diff_scroll\nkey CTRL+G\nscreenshot line_picker_ready\ntype "1"\nkey ENTER\n'
        setup += capture('initial', count)
        run(directory, 'probe', setup)
        initial = json.loads((directory / 'initial.json').read_text())
        side = 1 if mode == 'split_before' else 2 if mode == 'split_after' else 0
        sign = ' ' if source else '-' if mode in ['unified', 'split_before'] else '+'
        first = next(r for r in initial['reading_rows'] if r['line'] == 1 and r['side'] == side and r['sign'] == sign and r['offset'] == 0)
        x, y = first['content_x'], first['rect']['y'] + first['rect']['height'] / 2
        selecting = setup + f'drag_to {x} {y} {x} {y}\nfocus_ui diff_scroll\nkey CMD+HOME\n' + capture('start', count)
        selecting += 'key CMD+SHIFT+END\n' + capture('extent', count)
        if args.baseline:
            run(directory, 'baseline', selecting)
            snapshot = json.loads((directory / 'extent.json').read_text())
            assert snapshot['selection_text'] == '', (zoom, mode)
            print(f'PASS baseline {zoom}% {mode}: selection cannot copy across unrendered endpoints', flush=True)
            continue
        journey = selecting + 'key CMD+EQUAL\nresize 1600 1000\n' + capture('resized', count)
        if source:
            journey += 'key CMD+W\n' + capture('closed', count - 1)
            journey += 'key CMD+SHIFT+T\n' + capture('reopened', count)
        journey += 'focus_ui diff_scroll\nkey CMD+C\n' + capture('copied', count) + 'bench_frames 120\nexpect_p99_below 20\n'
        run(directory, 'journey', journey)
        version = 'original' if mode == 'commit' else 'staged' if mode in ['index', 'unified', 'split_before'] else 'working'
        expected = versions[version]
        for label in ['extent', 'resized', 'copied'] + (['reopened'] if source else []):
            tab = active(directory / (label + '.workspace.json'))
            selection = tab['selection']
            assert selection['anchor_line'] == 1 and selection['anchor_column'] == 1, (zoom, mode, label, selection)
            assert selection['head_line'] == 5000 and selection['head_column'] == len(expected.splitlines()[-1]) + 1, (zoom, mode, label, selection)
            assert selection['side'] == ('before' if mode in ['unified', 'split_before'] else 'after')
        copied = json.loads((directory / 'copied.json').read_text())
        assert any(n.get('text') == 'Copied selection' and n['rendered'] for n in copied['nodes'])
        if not args.snapshots: assert read_clipboard(directory, 'whole') == expected
        if mode == 'working':
            viewport = next(n['visible_rect'] for n in initial['nodes'] if n.get('name') == 'diff_scroll' and n['rendered'])
            edge_y = viewport['y'] + viewport['height'] - 2
            drag = setup + f'mouse_down {x} {y}\nwait_frames 2\nmouse_move {x} {edge_y}\nwait_frames 300\n'
            drag += capture('dragging', count) + 'mouse_up\n' + capture('released', count)
            drag += 'key CMD+C\n' + capture('drag_copied', count)
            run(directory, 'edge_drag', drag)
            selected = active(directory / 'drag_copied.workspace.json')['selection']
            assert selected['anchor_line'] == 1 and selected['anchor_column'] == 1
            last_initial = max(r['line'] for r in initial['reading_rows'] if viewport['y'] <= r['rect']['y'] < edge_y)
            assert selected['head_line'] > last_initial, (zoom, selected, last_initial)
            rows = expected.splitlines(keepends=True)
            drag_text = ''.join(rows[:selected['head_line'] - 1]) + rows[selected['head_line'] - 1][:selected['head_column'] - 1]
            if not args.snapshots: assert read_clipboard(directory, 'drag') == drag_text
            snapshot = json.loads((directory / 'drag_copied.json').read_text())
            assert not any(r['line'] == 1 for r in snapshot['reading_rows'])
            print(f'PASS {zoom}% edge drag: anchor survives leaving the rendered rows and copies the complete range', flush=True)
        assert git('diff') == before and git('diff', '--cached') == staged_before
        print(f'PASS {zoom}% {mode}: complete range across virtualization/pages, resize/font/reopen, exact copy and read-only contents', flush=True)
if not args.baseline and (args.limits_only or (100 in args.zooms and 'working' in args.modes)):
    repo = out / 'limit_fixture'
    repo.mkdir()
    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Limit fixture'),
                    ('config', 'user.email', 'limit@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*command)
    sentinel = marker + ' sentinel'
    (repo / 'sentinel.txt').write_text(sentinel)
    (repo / 'limit.txt').write_text(marker + ' ' + 'x' * (8 * 1024 * 1024 + 1))
    git('add', '.')
    git('commit', '-qm', 'Copy limit fixture')
    directory = out / 'limits'
    directory.mkdir()
    setup = 'resize 1800 1200\n' + capture('limit_ready', 1)
    setup += 'key CMD+P\nscreenshot seed_picker\ntype "sentinel.txt"\nkey ENTER\n' + capture('seed', 2)
    setup += 'focus_ui diff_scroll\nkey CMD+HOME\nkey CMD+SHIFT+END\n' + capture('seed_selected', 2)
    setup += 'key CMD+C\n' + capture('seed_copied', 2)
    setup += 'key CMD+P\nscreenshot limit_picker\ntype "limit.txt"\nkey ENTER\n' + capture('limit_open', 3)
    setup += 'focus_ui diff_scroll\nkey CMD+HOME\nkey CMD+SHIFT+END\n' + capture('limit_selected', 3)
    setup += 'key CMD+C\nkey CMD+W\n' + capture('cancelled', 2)
    run(directory, 'cancel', setup)
    if not args.snapshots: assert read_clipboard(directory, 'cancelled') == sentinel
    setup += 'key CMD+SHIFT+T\n' + capture('limit_reopened', 3)
    setup += 'focus_ui diff_scroll\nkey CMD+C\nkey CMD+T\n' + capture('repository_cancelled', 1)
    run(directory, 'repo_probe', setup)
    if not args.snapshots: assert read_clipboard(directory, 'repository_cancelled') == sentinel
    snapshot = json.loads((directory / 'repository_cancelled.json').read_text())
    closes = [n for n in snapshot['nodes'] if n.get('name') == 'tab_close' and n['rendered']]
    assert len(closes) == 2
    close = max(closes, key=lambda n: n['rect']['x'])['rect']
    setup += f"click {close['x'] + close['width'] / 2} {close['y'] + close['height'] / 2}\n" + capture('repository_returned', 3)
    setup += 'focus_ui diff_scroll\nkey CMD+C\n' + capture('refused', 3)
    run(directory, 'limit', setup)
    refused = json.loads((directory / 'refused.json').read_text())
    assert any(n.get('text') == 'Selection exceeds the 8 MiB copy limit' and n['rendered'] for n in refused['nodes'])
    if not args.snapshots: assert read_clipboard(directory, 'refused') == sentinel
    assert git('status', '--porcelain') == ''
    print('PASS copy limit and cancellation: clipboard remains unchanged, explicit 8 MiB refusal, source remains read-only', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, snapshots=args.snapshots, binary_sha256=digest), indent=2) + '\n')
