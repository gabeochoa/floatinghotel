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
for name in 'ab':
    (repo / f'{name}.cpp').write_text(''.join(f'const char* {name}_{i} = "' + 'café source text ' * 20 + '";\n' for i in range(300)))
(repo / 'notes.md').write_text(''.join(f'# Heading {i}\n\n' + 'café words   for wrapping ' * 30 + '\n```cpp\n' + 'std::string text = "café"; ' * 25 + '\n```\n' for i in range(100)))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Anchor layout fixture'),
                ('config', 'user.email', 'anchor@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Reading anchor layout')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name, count):
    return f'wait_frames 20\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


def picker(path):
    return f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\nkey ENTER\nwait_for_refresh\n'

script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
script += picker('a.cpp') + 'hover_ui diff_scroll\nscroll_wheel 0 -37\n' + capture('source', 2)
script += 'native_menu_action "Zoom In"\n' * 4 + capture('zoomed', 2)
script += 'native_menu_action "Zoom In"\n' * 6 + capture('zoom200', 2)
script += 'native_menu_action "Zoom Out"\n' * 6
script += 'resize 1250 850\n' + capture('narrow', 2)
script += 'key CMD+EQUAL\nkey CMD+EQUAL\n' + capture('font', 2)
script += picker('b.cpp') + 'native_menu_action "Reset Zoom"\nresize 1600 1000\nwait_frames 5\n'
script += 'click_ui open_tabs_menu\nwait_frames 3\nclick_ui "context_menu_item_a.cpp"\nwait_for_refresh\n' + capture('reactivated', 3)
script += 'click_text "Reading anchor layout"\nwait_for_refresh\nkey ENTER\nclick_ui "jump_to_diff:a.cpp"\nwait_frames 8\nhover_ui commit_detail_scroll\nscroll_wheel 0 -29\n' + capture('review', 4)
script += 'click_text "Split"\n' + capture('split', 4)
script += 'native_menu_action "Zoom In"\n' * 4 + capture('split_zoom', 4)
script += 'native_menu_action "Zoom In"\n' * 6 + capture('split200', 4)
script += 'native_menu_action "Reset Zoom"\nclick_text "Unified"\nclick_ui "jump_to_diff:b.cpp"\nwait_frames 8\nclick_ui "jump_to_diff:a.cpp"\n' + capture('before_fold', 4)
script += 'click_ui "fold_file:a.cpp"\n' + capture('folded', 4)
script += 'click_ui "fold_file:a.cpp"\n' + capture('unfolded', 4)
script += picker('notes.md') + 'click_ui markdown_preview_toggle\nhover_ui markdown_preview\nscroll_wheel 0 -37\n' + capture('markdown', 5)
script += 'native_menu_action "Zoom In"\n' * 4 + capture('markdown140', 5)
script += 'native_menu_action "Zoom In"\n' * 6 + capture('markdown200', 5)
script += 'resize 1250 850\n' + capture('markdown_narrow', 5)
script += 'hover_ui markdown_preview\nscroll_wheel 0 -10\n' + capture('markdown_scrolled', 5)
script += 'native_menu_action "Reset Zoom"\n' + capture('markdown_reset', 5)
script += 'bench_frames 120\nexpect_p99_below 20\n'
path = out / 'journey.e2e'
path.write_text(script)
with (out / 'run.log').open('w') as log:
    result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
        f'--test-script={path}', f'--screenshot-dir={out}', '--e2e-timeout=180'], cwd=ROOT,
        env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
assert result.returncode == 0, out / 'run.log'


def anchor(name):
    data = json.loads((out / f'{name}.workspace.json').read_text())
    return data['history'][data['history_index']]['anchor']


def error(before, after):
    saved = anchor(before)
    data = json.loads((out / f'{after}.json').read_text())
    view = next(n for n in data['nodes'] if n.get('name') in ('diff_scroll', 'commit_detail_scroll', 'markdown_preview') and n['rendered'])
    viewport = view['rect']
    original = (repo / saved['path']).read_text().splitlines()[saved['line'] - 1]
    byte = len(original[:saved['column'] - 1].encode())
    rows = [r for r in (data['reading_rows'] if view['name'] != 'markdown_preview' else []) if r['path'] == saved['path'] and r['line'] == saved['line']
            and r['offset'] <= byte < r['offset'] + len(r['text'].encode()) and r['sign'] == saved['sign']]
    rows += [r for r in data.get('reading_projections', []) if not r['folded'] and r['path'] == saved['path'] and r['line'] == saved['line'] and r['column'] <= saved['column'] <= r['end_column']]
    expected = viewport['y'] + viewport['height'] * saved['fraction']
    return min((abs(r['rect']['y'] - expected) for r in rows), default=None)

errors = {after: error(before, after) for before, after in [('source', 'zoomed'), ('source', 'narrow'),
    ('source', 'font'), ('source', 'zoom200'), ('source', 'reactivated'), ('review', 'split'), ('review', 'split_zoom'), ('review', 'split200'),
    ('before_fold', 'unfolded'), ('markdown', 'markdown140'), ('markdown', 'markdown200'), ('markdown', 'markdown_narrow'), ('markdown_scrolled', 'markdown_reset')]}
if not args.baseline:
    assert anchor('before_fold') == anchor('folded')
    assert anchor('markdown_scrolled')['line'] > anchor('markdown')['line']
report = dict(binary_sha256=digest, baseline=args.baseline, errors_px=errors)
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(report, flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
if not args.baseline:
    assert all(value is not None and value < 2 for value in errors.values()), errors
