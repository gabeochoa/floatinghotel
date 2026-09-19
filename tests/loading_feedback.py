import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
parser.add_argument('--snapshots', action='store_true')
parser.add_argument('--cancel-buttons', action='store_true')
parser.add_argument('--zooms', nargs='+', type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
def git(*args): return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Loading fixture'),
        ('config', 'user.email', 'loading@example.invalid'), ('config', 'commit.gpgsign', 'false')]: git(*command)
for name in ['a', 'b', 'c']:
    (repo / f'{name}.cpp').write_text(f'int ONLY_{name.upper()} = 1;\n')
git('add', '.'); git('commit', '-qm', 'Initial review')
(repo / 'a.cpp').write_text('int SLOW_VERSION = 2;\n')
git('add', '.'); git('commit', '-qm', 'Slow review')
slow = git('rev-parse', 'HEAD')
(repo / 'a.cpp').write_text('int FAST_VERSION = 3;\n')
git('add', '.'); git('commit', '-qm', 'Fast review')
head = git('rev-parse', 'HEAD')
wrapper = out / 'bin'
wrapper.mkdir()
real_git = shutil.which('git')
(wrapper / 'git').write_text('#!/usr/bin/env python3\nimport os, sys, time\nfrom pathlib import Path\n'
    + f"slow = {slow!r}\nhead = {head!r}\n"
    + "kind = 'commit' if '--git-common-dir' in sys.argv and any(a.startswith(slow) for a in sys.argv) else 'source' if 'ls-tree' in sys.argv and ':(literal)b.cpp' in sys.argv else 'comparison' if 'diff' in sys.argv and '--no-ext-diff' in sys.argv and slow in sys.argv and head in sys.argv else ''\n"
    + "if kind:\n    folder = Path(os.environ['FH_LOADING_EVENTS'])\n    (folder / (kind + '.started')).touch()\n    time.sleep(.2)\n    (folder / (kind + '.busy')).touch()\n    time.sleep(8)\n    (folder / (kind + '.finished')).touch()\n"
    + f"os.execv({real_git!r}, [{real_git!r}, *sys.argv[1:]])\n")
(wrapper / 'git').chmod(0o755)
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
def capture(label, count, wait=True):
    return ('wait_for_refresh\nwait_frames 15\n' if wait else '') + f'workspace_checkpoint {count} {label}\nscreenshot {label}\n'
def picker(name):
    return f'key CMD+P\nwait_for_refresh\nscreenshot picker\nclick_ui file_picker_input\nkey CMD+A\ntype "{name}"\nkey ENTER\n'
results = []
for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'; settings.mkdir()
    script = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'screenshot ready\nclick_text "Fast review"\nkey ENTER\n' + capture('fast', 2)
    script += picker('c.cpp') + capture('warm_source', 3) + 'click_ui content_document_2\n' + capture('warm_review', 3)
    script += 'click_text "Slow review"\n' + capture('commit_pending', 4, False)
    script += 'wait_for_path commit.busy\n' + capture('commit_busy', 4, False)
    if args.cancel_buttons: script += 'click_ui cancel_active_read\nwait_for_refresh\n' + capture('commit_cancelled', 4)
    script += 'click_text "Fast review"\n' + capture('commit_return', 4)
    script += picker('b.cpp') + capture('source_pending', 5, False)
    script += 'wait_for_path source.busy\n' + capture('source_busy', 5, False)
    if args.cancel_buttons: script += 'click_ui cancel_active_read\nwait_for_refresh\n' + capture('source_cancelled', 5)
    script += 'click_ui content_document_3\n' + capture('source_return', 5)
    script += 'native_menu_action "Compare Revisions..."\nwait_frames 3\nscreenshot comparison_form\n'
    script += f'click_ui compare_base\nkey CMD+A\ntype "{slow}"\nclick_ui compare_target\nkey CMD+A\ntype "{head}"\nclick_ui compare_submit\n'
    script += capture('comparison_pending', 5, False) + 'wait_for_path comparison.busy\n' + capture('comparison_busy', 5, False)
    if args.cancel_buttons: script += 'click_ui cancel_active_read\nwait_for_refresh\n' + capture('comparison_cancelled', 5)
    script += 'click_ui content_document_3\n' + capture('comparison_return', 5)
    script += 'wait_frames 700\n' + capture('settled', 5)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'; path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', *(['--headless'] if args.snapshots else []),
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_NATIVE_HIDDEN='1', FH_TEST_SETTINGS_DIR=str(settings),
                FH_LOADING_EVENTS=str(directory), PATH=str(wrapper) + ':' + os.environ['PATH']),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def layout(name): return json.loads((directory / f'{name}.json').read_text())
    def nodes(name): return [n for n in layout(name)['nodes'] if n['rendered']]
    def text(name, control): return next(n.get('text') for n in nodes(name) if n.get('name') == control)
    for name in ['commit_pending', 'commit_busy']:
        assert text(name, 'commit_detail_subject') == 'Slow review'
        assert not layout(name)['reading_rows'], (zoom, name, 'old commit contents')
    assert text('commit_busy', 'commit_detail_loading') == 'Loading commit details...'
    assert text('commit_return', 'commit_detail_subject') == 'Fast review'
    for name in ['source_pending', 'source_busy']:
        assert text(name, 'full_file_path') == 'b.cpp'
        assert not layout(name)['reading_rows'], (zoom, name, 'old source contents')
    assert text('source_busy', 'full_file_loading') == 'Loading file...'
    for name in ['source_return', 'comparison_return', 'settled']:
        assert text(name, 'full_file_path') == 'c.cpp'
        assert any('ONLY_C' in r['text'] for r in layout(name)['reading_rows'])
        assert not any('ONLY_B' in r['text'] for r in layout(name)['reading_rows'])
        assert not any(n.get('name') == 'full_file_loading' for n in nodes(name))
    for name in ['comparison_pending', 'comparison_busy']:
        assert not layout(name)['reading_rows'], (zoom, name, 'old comparison contents')
    assert any(n.get('text') == 'Comparing revisions...' or (args.cancel_buttons and n.get('text') == 'Loading comparison...') for n in nodes('comparison_busy'))
    for name, control in [('commit_busy', 'commit_detail_loading'), ('source_busy', 'full_file_loading'), ('comparison_busy', 'comparison_loading_status')]:
        rendered = nodes(name)
        main = next(n['rect'] for n in rendered if n.get('name') == 'main_content')
        indicator = next(n['rect'] for n in rendered if n.get('name') == control or (name == 'comparison_busy' and n.get('text') == 'Comparing revisions...'))
        assert indicator['width'] > 0 and indicator['height'] > 0
        assert indicator['x'] >= main['x'] - .1 and indicator['y'] >= main['y'] - .1
        assert indicator['x'] + indicator['width'] <= main['x'] + main['width'] + .1
        assert indicator['y'] + indicator['height'] <= main['y'] + main['height'] + .1
    header = next(n['rect'] for n in nodes('source_busy') if n.get('name') == 'full_file_header')
    assert abs(header['height'] - 32 * zoom / 100) < .1
    assert not (directory / 'comparison.finished').exists(), 'superseded comparison was not cancelled'
    assert not (directory / 'commit.finished').exists(), 'superseded commit read was not cancelled'
    assert not (directory / 'source.finished').exists(), 'superseded source read was not cancelled'
    if args.cancel_buttons:
        for kind in ['commit', 'source', 'comparison']:
            assert any(n.get('name') == 'cancel_active_read' for n in nodes(kind + '_busy'))
            assert not any(n.get('name') == 'cancel_active_read' for n in nodes(kind + '_cancelled'))
    if not args.snapshots: assert 'Native test window hidden=1 key=0' in (directory / 'run.log').read_text()
    results.append(dict(zoom=zoom, cancelled=['commit', 'source', 'comparison']))
    print(f'PASS {zoom}% immediate destination headings, delayed reads, clean loading surfaces and cancellation', flush=True)
assert not git('status', '--porcelain')
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, results=results), indent=2) + '\n')
