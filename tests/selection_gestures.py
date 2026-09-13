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

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Selection fixture'),
                ('config', 'user.email', 'selection@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
word = 'wrap_éλ_value' * 12
old = 'alpha_éλ beta\tgamma ' + word + ';'
new = old.replace('alpha', 'delta')
(repo / 'a.cpp').write_text('int one = 1;\nint two = 2;\n' + old + '\nint four = 4;\n')
git('add', '.')
git('commit', '-qm', 'Selection baseline')
(repo / 'a.cpp').write_text((repo / 'a.cpp').read_text().replace(old, new))
before = git('diff')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def settle():
    return 'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 10\n'

def capture(label, count):
    return settle() + f'workspace_checkpoint {count} {label}\nscreenshot {label}\n'

for zoom in [100, 140, 200]:
    for mode in ['source', 'unified', 'split']:
        directory = out / f'{zoom}-{mode}'
        directory.mkdir()
        count = 2 if mode == 'source' else 1
        setup = 'resize 1800 1400\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        setup += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:a.cpp\n'
        if mode == 'source':
            setup += 'key CMD+P\nwait_for_refresh\nclick_ui file_picker_input\ntype "a.cpp:3:1"\nkey ENTER\n'
        else:
            if mode == 'split':
                setup += 'click_text "Split"\n'
            setup += settle() + 'click_ui hunk_header_label\nkey CTRL+G\nwait_frames 4\nclick_ui line_picker_input\ntype "3"\nkey ENTER\n'
        setup += capture('initial', count)
        def replay(label, script):
            path = directory / f'{label}.e2e'
            path.write_text(script)
            with (directory / f'{label}.log').open('w') as log:
                result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
                    f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
                    env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
            assert result.returncode == 0, (zoom, mode, directory / f'{label}.log')
        def layout(label):
            return json.loads((directory / f'{label}.json').read_text())
        replay('probe', setup)
        initial = layout('initial')
        nodes = {n['id']: n for n in initial['nodes']}
        sides = [2, 1] if mode == 'split' else [0]
        for side in sides:
            sign = '-' if side == 1 else '+' if mode != 'source' else ' '
            expected_line = old if side == 1 else new
            rows = [r for r in initial['reading_rows'] if r['path'] == 'a.cpp' and r['line'] == 3 and r['side'] == side and r['sign'] == sign]
            assert ''.join(r['text'] for r in rows) == expected_line, (zoom, mode, side, rows)
            visible = [r for r in rows if nodes[r['id']]['rendered'] and nodes[r['id']]['visible_rect']['height'] >= r['rect']['height'] - .1]
            assert len(visible) >= 2, (zoom, mode, side, visible)
            first, second = visible[:2]
            assert first['offset'] == 0, (zoom, mode, side)
            def xy(row):
                return row['content_x'] + .2, row['rect']['y'] + row['rect']['height'] / 2
            x1, y1 = xy(first)
            x2, y2 = xy(second)
            label = f'word_{side}'
            replay(label, setup + f'double_click {x1} {y1}\n' + capture(label, count))
            selected = layout(label)['selection_text']
            if args.baseline:
                assert selected == '', (zoom, mode, side, selected)
                continue
            assert selected == ('alpha_éλ' if side == 1 else 'delta_éλ'), (zoom, mode, side, selected)
            label = f'wrapped_word_{side}'
            replay(label, setup + f'double_click {x2} {y2}\n' + capture(label, count))
            selected = layout(label)['selection_text']
            assert selected == word, (zoom, mode, side, selected)
            label = f'line_{side}'
            replay(label, setup + f'triple_click {x2} {y2}\n' + capture(label, count))
            value = layout(label)
            assert value['selection_text'] == expected_line, (zoom, mode, side, value['selection_text'])
            assert value['selection_location'] == 'a.cpp:L3\n' + expected_line
            highlights = [n for n in value['nodes'] if n.get('name') == 'diff_sel_hl' and n['rendered'] and n['visible_rect']['height'] > 0]
            assert len(highlights) >= 2
            for highlight in highlights:
                parent = next(n for n in value['nodes'] if n['id'] == highlight['parent'])
                assert highlight['rect']['x'] >= parent['rect']['x']
                assert highlight['rect']['x'] + highlight['rect']['width'] <= parent['rect']['x'] + parent['rect']['width'] + 1
            label = f'shift_{side}'
            replay(label, setup + f'drag_to {x1} {y1} {x1} {y1}\nhold_key 340\ndrag_to {x2} {y2} {x2} {y2}\nrelease_key 340\n'
                + capture(label, count) + 'hover_ui diff_scroll\nscroll_wheel -10 0\n' + capture(f'horizontal_{side}', count) + 'bench_frames 120\nexpect_p99_below 20\n')
            assert layout(label)['selection_text'] == expected_line.encode()[:second['offset']].decode(), (zoom, mode, side)
            assert layout(f'horizontal_{side}')['selection_text'] == layout(label)['selection_text']
            assert layout(f'horizontal_{side}')['reading_rows'] == layout(label)['reading_rows']
            assert git('diff') == before and git('diff', '--cached') == ''
        print(f'PASS {zoom}% {mode}: ' + ('baseline has no word gesture' if args.baseline else 'word, wrapped identifier, logical line, Shift extension, exact Unicode/tab bytes and bounded highlight geometry'), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest), indent=2) + '\n')
