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
args = parser.parse_args()
guard = ROOT / 'output/clipboard_guard'
marker = os.environ.get('FH_TEST_CLIPBOARD_MARKER')
if not marker:
    subprocess.run(['swiftc', str(ROOT / 'tests/clipboard_guard.swift'), '-o', str(guard)], check=True)
    marker = 'FH47_' + uuid.uuid4().hex
    raise SystemExit(subprocess.run([str(guard), 'guard', marker, sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]).returncode)
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Copy fixture'),
                ('config', 'user.email', 'copy@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
second = 'second\tline   \r\n'
versions = {name: '\t' + marker + ' ' + name + '_éλ  \r\n' + second for name in ['original', 'staged', 'working']}
(repo / 'copy.cpp').write_bytes((versions['original'] + 'tail').encode())
git('add', '.')
git('commit', '-qm', 'Copy fixture base')
commit = git('rev-parse', 'HEAD')
(repo / 'copy.cpp').write_bytes((versions['staged'] + 'tail').encode())
git('add', '.')
(repo / 'copy.cpp').write_bytes((versions['working'] + 'tail').encode())
before, staged_before = git('diff'), git('diff', '--cached')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def settle():
    return 'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 12\n'

for zoom in [100, 140, 200]:
    for mode in ['working', 'index', 'commit', 'unified', 'split_before', 'split_after']:
        directory = out / f'{zoom}-{mode}'
        directory.mkdir()
        source = mode in ['working', 'index', 'commit']
        count = (2 if mode == 'working' else 3) if source else 1
        setup = 'resize 1800 1400\nwait_frames 15\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        setup += 'screenshot zoom_ready\nclick_ui review_unstaged_changes\nwait_for_refresh\nwait_frames 15\nscreenshot changes_ready\nclick_ui jump_to_diff:copy.cpp\n'
        if mode == 'index': setup += 'click_ui review_staged_changes\nwait_for_refresh\nscreenshot scope_ready\n'
        if mode == 'commit': setup += 'click_text "Copy fixture base"\nwait_for_refresh\nscreenshot scope_ready\n'
        if source:
            setup += 'key CMD+P\nwait_for_refresh\nwait_frames 15\nscreenshot picker_ready\nclick_ui file_picker_input\ntype "copy.cpp"\nkey ENTER\n'
        else:
            if mode.startswith('split'): setup += 'click_text "Split"\nscreenshot split_ready\n'
            setup += settle() + 'click_ui hunk_header_label\nkey CTRL+G\nwait_frames 4\nscreenshot line_ready\nclick_ui line_picker_input\ntype "1"\nkey ENTER\n'
        setup += settle() + f'workspace_checkpoint {count} initial\nscreenshot initial\n'
        def replay(label, script, clipboard=False):
            settings = directory / (label + '_settings')
            settings.mkdir()
            (settings / 'settings.json').write_text(json.dumps({'copy_with_location': label != 'location_key'}))
            path = directory / (label + '.e2e')
            path.write_text(script)
            with (directory / (label + '.log')).open('w') as log:
                result = subprocess.run([str(binary), str(repo), '--test-mode', *(['--headless'] if args.baseline or args.snapshots else []), f'--test-script={path}',
                    f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
                    env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_NATIVE_HIDDEN='1', FH_TEST_SETTINGS_DIR=str(settings)), stdout=log, stderr=subprocess.STDOUT, timeout=210)
            assert result.returncode == 0, (zoom, mode, label, directory / (label + '.log'))
            if not args.baseline and not args.snapshots:
                assert 'Native test window hidden=1 key=0' in (directory / (label + '.log')).read_text()
            if clipboard and (args.baseline or args.snapshots):
                field = 'selection_location' if args.baseline or label.startswith('location') else 'selection_text'
                return json.loads((directory / 'selected.json').read_text())[field]
            if clipboard:
                result = directory / (label + '.clipboard.json')
                subprocess.run([str(guard), 'read', marker, str(result)], check=True)
                return json.loads(result.read_text())['text']
        replay('probe', setup)
        initial = json.loads((directory / 'initial.json').read_text())
        side = 1 if mode == 'split_before' else 2 if mode == 'split_after' else 0
        sign = ' ' if source else '-' if mode == 'split_before' else '+'
        first = next(r for r in initial['reading_rows'] if r['line'] == 1 and r['side'] == side and r['sign'] == sign and r['offset'] == 0)
        last = next(r for r in initial['reading_rows'] if r['line'] == 3 and r['side'] == side and r['sign'] == ' ' and r['offset'] == 0)
        x1, y1 = first['content_x'], first['rect']['y'] + first['rect']['height'] / 2
        x2, y2 = last['content_x'], last['rect']['y'] + last['rect']['height'] / 2
        selection = setup + f'drag_to {x1} {y1} {x2} {y2}\n' + settle() + 'screenshot selected\n'
        version = 'original' if mode == 'commit' else 'staged' if mode in ['index', 'split_before'] else 'working'
        plain = versions[version]
        located = 'copy.cpp:L1-3\n' + plain
        commands = [('plain_key', 'key CMD+C', plain), ('location_key', 'key CMD+SHIFT+C', located),
                    ('plain_menu', 'native_menu_action "Copy"', plain), ('location_menu', 'native_menu_action "Copy with location"', located)]
        if args.snapshots:
            commands = commands[:2]
        if args.baseline:
            commands = [('baseline', 'key CMD+C', located)]
        for label, command, expected in commands:
            copied = replay(label, selection + command + '\nwait_frames 6\nscreenshot ' + label + '\n', True)
            assert copied == expected, (zoom, mode, label, repr(copied), repr(expected))
            snapshot = json.loads((directory / 'selected.json').read_text())
            assert snapshot['selection_text'] == plain
            after = json.loads((directory / (label + '.json')).read_text())
            def viewport(value):
                return next(n['rect'] for n in value['nodes'] if n.get('name') == 'diff_scroll' and n['rendered'])
            assert viewport(initial) == viewport(snapshot) == viewport(after)
            if not args.baseline:
                message = 'Copied selection with location' if label.startswith('location') else 'Copied selection'
                assert any(n.get('text') == message and n['rendered'] for n in after['nodes'])
            if source:
                state = json.loads((directory / 'initial.workspace.json').read_text())
                active = next(t for t in state['tabs'] if t['id'] == state['active'])
                revision = commit if mode == 'commit' else 'INDEX' if mode == 'index' else ''
                assert active['revision'] == revision
                badge = commit[:7] if mode == 'commit' else 'Index' if mode == 'index' else 'Working tree'
                assert any(n.get('name') == 'full_file_revision' and n.get('text') == badge and n['rendered'] for n in snapshot['nodes'])
            assert git('diff') == before and git('diff', '--cached') == staged_before
        print(f'PASS {zoom}% {mode}: ' + ('baseline selection format includes location (headless clipboard unavailable)' if args.baseline else 'offscreen selection and geometry' if args.snapshots else 'native clipboard bytes, distinct keys/menu commands, legacy preference ignored, revision identity and CRLF/tab preservation'), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, snapshots=args.snapshots, binary_sha256=digest), indent=2) + '\n')
