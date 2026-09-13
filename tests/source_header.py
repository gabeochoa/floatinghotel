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
parser.add_argument('--compare', type=Path)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
source = 'nested/long_parent_name/another_directory/reader.cpp'
(repo / source).parent.mkdir(parents=True)
(repo / source).write_text(''.join(f'int header_line_{i:03d} = {i};\n' for i in range(1, 151)))
(repo / 'notes.md').write_text('# Header notes\n\nRead the complete document.\n')
(repo / 'utf16.txt').write_bytes('hello utf16\n'.encode('utf-16'))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Header fixture'),
                ('config', 'user.email', 'header@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Source header fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name, count=3):
    return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


def picker(path):
    return f'key CMD+P\nwait_for_refresh\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\nkey ENTER\nwait_for_refresh\nwait_frames 10\n'


def menu(label):
    return f'click_ui full_file_options\nwait_frames 3\nclick_ui "context_menu_item_{label}"\n'


for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Source header fixture"\nwait_for_refresh\nkey ENTER\n' + capture('review', 2)
    script += picker(source) + capture('source')
    script += 'resize 1150 850\n' + capture('narrow')
    if not args.baseline:
        script += 'bench_frames 120\nexpect_p99_below 20\n'
        script += 'click_ui full_file_options\n' + capture('menu')
        script += 'key ESCAPE\n' + capture('dismissed')
        script += menu('Bookmark line 1') + capture('bookmark')
        script += menu('Remove bookmark') + capture('removed')
        script += 'click_text "int header_line_001 = 1;"\nkey SHIFT+HOME\n' + menu('Blame line 1') + capture('blame')
        script += 'expect_text "Header fixture"\nexpect_text "Source header fixture"\nclick_text Close\n'
        script += menu('File history') + capture('history')
        script += 'expect_text "History · ' + source + ' · follows renames"\nclick_text Back\n'
        script += 'click_ui full_file_back\n' + capture('returned')
        script += picker('notes.md') + menu('Preview Markdown') + capture('markdown', 4)
        script += 'assert_ui markdown_preview hidden=false\n'
        script += menu('Raw Markdown') + capture('raw', 4)
        script += 'assert_ui diff_scroll hidden=false\n'
        script += picker('utf16.txt') + 'click_ui full_file_options\n' + capture('auto', 5)
        script += 'expect_text "UTF-16 LE"\nclick_ui context_menu_item_Encoding\nwait_frames 3\nclick_ui context_menu_item_UTF-8\n'
        script += capture('utf8', 5)
        script += menu('Encoding') + 'wait_frames 3\nclick_ui "context_menu_item_UTF-16 LE"\n' + capture('utf16', 5)
        script += 'expect_text "hello utf16"\n'
        script += 'click_ui full_file_options\nwait_frames 3\nkey DOWN\n' + capture('keyboard_menu', 5)
        script += 'key ENTER\n' + capture('keyboard_history', 5)
        script += 'expect_text "History · utf16.txt · follows renames"\n'
        script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, (zoom, directory / 'run.log')

    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())

    def node(name, debug):
        return next(n for n in layout(name)['nodes'] if n.get('name') == debug and n['rendered'])

    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())

    metrics = {}
    for name in ['source', 'narrow']:
        header = node(name, 'full_file_header')['rect']
        viewport = node(name, 'diff_scroll')['rect']
        row = layout(name)['reading_rows'][0]
        font = next(n['font_size']['value'] for n in layout(name)['nodes'] if n['id'] == row['id'])
        metrics[name] = dict(header=header, viewport=viewport, code_font=font)
        if args.compare:
            prior = json.loads((args.compare / str(zoom) / f'{name}.json').read_text())
            old_viewport = next(n['rect'] for n in prior['nodes'] if n.get('name') == 'diff_scroll' and n['rendered'])
            old_row = prior['reading_rows'][0]
            old_font = next(n['font_size']['value'] for n in prior['nodes'] if n['id'] == old_row['id'])
            assert font == old_font and abs(font - 17.6) < .001
            assert abs(viewport['height'] - old_viewport['height'] - 34 * zoom / 100) < 1
            assert abs(old_viewport['y'] - viewport['y'] - 34 * zoom / 100) < 1
        if args.baseline:
            continue
        assert abs(header['height'] - 32 * zoom / 100) < 1, (zoom, name, header)
        assert abs(viewport['y'] - header['y'] - header['height']) < 1, (zoom, name, viewport)
        children = [node(name, debug)['rect'] for debug in ['full_file_back', 'full_file_path', 'full_file_revision', 'full_file_options']]
        for rect in children:
            assert rect['width'] > 0 and rect['x'] >= header['x'] and rect['x'] + rect['width'] <= header['x'] + header['width'] + 1
            assert header['y'] <= rect['y'] and rect['y'] + rect['height'] <= header['y'] + header['height'] + 1
        for left, right in zip(children, children[1:]):
            assert left['x'] + left['width'] <= right['x'] + 1
        assert not any(n.get('name') == 'full_file_actions' and n['rendered'] for n in layout(name)['nodes'])
    if not args.baseline:
        assert layout('narrow')['reading_rows'] == layout('dismissed')['reading_rows']
        focus = next(n['focus_target'] for n in layout('dismissed')['nodes'] if n['focused'] and 'focus_target' in n)
        assert focus['control'] == 'full_file_options', (zoom, focus)
        assert state('returned')['active'] == state('review')['active']
        assert state('returned')['history'][-1]['anchor'] == state('review')['history'][-1]['anchor']
        assert node('bookmark', 'diff_scroll')['rect'] == node('narrow', 'diff_scroll')['rect']
        assert node('removed', 'diff_scroll')['rect'] == node('narrow', 'diff_scroll')['rect']
        assert state('utf8')['tabs'][-1]['revision'] == state('utf16')['tabs'][-1]['revision']
    (directory / 'geometry.json').write_text(json.dumps(metrics, indent=2) + '\n')
    print(f'PASS {zoom}% source header' + (' baseline' if args.baseline else ', menus, encoding, attribution, history, origin and narrow geometry'), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest), indent=2) + '\n')
