import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Caret fixture'),
                ('config', 'user.email', 'caret@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
text = 'const char* words = "' + 'éλ words\tand spaces 🙂 é ' * 6 + 'END";'
(repo / 'a.cpp').write_text('int first = 1;\nint second = 2;\nint third = 3;\n' + text + '\nint fifth = 5;\n')
(repo / 'b.cpp').write_text('int other = 1;\n')
(repo / 'empty.txt').write_text('')
git('add', '.')
git('commit', '-qm', 'Caret baseline')
p = repo / 'a.cpp'
p.write_text(p.read_text().replace('char* words', 'char* new_words'))
before = git('diff')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def settle():
    return 'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 12\n'

def capture(name, count):
    return settle() + f'workspace_checkpoint {count} {name}\nscreenshot {name}\n'

def open_file(path):
    return f'key CMD+P\nwait_for_refresh\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 6\nkey ENTER\n' + settle()

for zoom in [100, 140, 200]:
    for mode in ['source', 'unified', 'split']:
        directory = out / f'{zoom}-{mode}'
        directory.mkdir()
        count = 2 if mode == 'source' else 1
        setup = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        setup += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:a.cpp\n'
        if mode == 'source':
            setup += open_file('a.cpp:4:1')
        elif mode == 'split':
            setup += 'click_text "Split"\n'
        if zoom == 200 and mode != 'source':
            setup += settle() + 'click_ui hunk_header_label\nkey CTRL+G\nwait_frames 4\nclick_ui line_picker_input\ntype "4"\nkey ENTER\n'
        setup += capture('initial', count)
        def replay(label, script):
            path = directory / f'{label}.e2e'
            path.write_text(script)
            with (directory / f'{label}.log').open('w') as log:
                result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
                    f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
                    env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
            assert result.returncode == 0, (zoom, mode, directory / f'{label}.log')
        def layout(name):
            return json.loads((directory / f'{name}.json').read_text())
        def active(name):
            value = json.loads((directory / f'{name}.workspace.json').read_text())
            return next(t for t in value['tabs'] if t['id'] == value['active'])
        def visible(name, debug):
            return [n for n in layout(name)['nodes'] if n.get('name') == debug and n['rendered'] and n['visible_rect']['height'] > 0 and n['visible_rect']['width'] > 0]
        replay('probe', setup)
        if args.baseline:
            assert not visible('initial', 'code_caret')
            print(f'PASS baseline {zoom}% {mode}', flush=True)
            continue
        initial = layout('initial')
        nodes = {n['id']: n for n in initial['nodes']}
        side = 1 if mode == 'split' else 0
        sign = '-' if mode == 'split' else '+' if mode == 'unified' else ' '
        rows = [r for r in initial['reading_rows'] if r['path'] == 'a.cpp' and r['line'] == 4 and r['side'] == side and r['sign'] == sign
                and nodes[r['id']]['visible_rect']['height'] >= r['rect']['height'] - .1]
        assert len(rows) >= 2, (zoom, mode, rows)
        target = rows[1]
        x, y = target['content_x'], target['rect']['y'] + target['rect']['height'] / 2
        click = f'drag_to {x} {y} {x} {y}\n'
        script = setup + click + capture('clicked', count)
        script += 'mouse_move 1 1\n' + capture('left', count)
        script += 'key CMD+EQUAL\n' + capture('font', count)
        script += 'resize 1200 1100\n' + capture('narrow', count)
        script += 'resize 1600 1100\nnative_menu_action "Reset Code Text"\nkey CMD+F\nwait_frames 3\ntype "words"\n' + capture('find', count)
        script += 'key ESCAPE\n' + capture('find_closed', count)
        if mode == 'source':
            script += open_file('b.cpp') + capture('other', 3)
            script += 'click_ui content_document_2\n' + capture('returned', 3)
            script += open_file('empty.txt') + 'click_ui diff_line\n' + capture('empty', 4)
            script += 'key CMD+W\nkey CMD+SHIFT+T\n' + settle() + 'click_ui diff_line\n' + capture('reopened', 4)
        script += 'bench_frames 120\nexpect_p99_below 20\n'
        replay('journey', script)
        expected = {'path': 'a.cpp', 'line': 4, 'column': target['column'], 'side': 'before' if mode == 'split' else 'after'}
        for name in ['clicked', 'left', 'font', 'narrow']:
            assert active(name)['caret'] == expected, (zoom, mode, name, active(name))
            assert len(visible(name, 'code_caret')) == 1, (zoom, mode, name, visible(name, 'code_caret'))
            assert visible(name, 'code_active_gutter'), (zoom, mode, name)
        caret = visible('clicked', 'code_caret')[0]['rect']
        assert abs(caret['x'] - x) < 1, (zoom, mode, caret, target)
        assert abs(caret['y'] - target['rect']['y'] - 3 * zoom / 100) < 1
        assert abs(caret['width'] - 1.5 * zoom / 100) < .1
        assert abs(caret['height'] - (target['rect']['height'] - 6 * zoom / 100)) < 1
        assert visible('clicked', 'code_caret')[0]['rect'] == visible('left', 'code_caret')[0]['rect']
        if mode != 'source' and not (zoom == 200 and mode != 'source'):
            row = next(r for r in layout('clicked')['reading_rows'] if r['line'] == 4 and r['side'] == side and r['sign'] == sign and r['offset'] == target['offset'])
            colored = next(n for n in layout('clicked')['nodes'] if n['id'] == row['id'])
            assert colored['background'] != nodes[target['id']]['background']
        assert not visible('find', 'code_caret'), (zoom, mode, 'Find must own focus')
        assert visible('find_closed', 'code_caret'), (zoom, mode, 'Find return')
        if mode == 'source':
            assert active('returned')['caret'] == active('find_closed')['caret']
            assert active('other')['caret']['path'] == 'b.cpp' and active('other')['caret']['line'] == 1
            for name in ['empty', 'reopened']:
                assert active(name)['caret'] == {'path': 'empty.txt', 'line': 1, 'column': 1, 'side': 'after'}
                assert len(visible(name, 'code_caret')) == 1, (zoom, mode, name)
            assert visible('clicked', 'full_file_header')[0]['rect']['height'] == visible('font', 'full_file_header')[0]['rect']['height']
        assert git('diff') == before and git('diff', '--cached') == ''
        print(f'PASS {zoom}% {mode}: logical caret, active gutter, pointer geometry, font/resize, Find and read-only behavior', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest), indent=2) + '\n')
