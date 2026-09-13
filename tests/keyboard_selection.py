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
parser.add_argument('--offset-context', action='store_true')
parser.add_argument('--modes', nargs='+', choices=['source', 'unified', 'split', 'unified_before', 'split_before'], default=['source', 'unified', 'split', 'unified_before', 'split_before'])
parser.add_argument('--zooms', nargs='+', type=int, choices=[100, 140, 200], default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Keyboard fixture'),
                ('config', 'user.email', 'keyboard@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
lines = ['éλ_alpha beta\tgamma', 'é🙂 Z', '', 'wrapped_' * 30] + [f'int line_{i} = {i};' for i in range(5, 101)]
(repo / 'a.cpp').write_text('\n'.join(lines) + '\n')
(repo / 'large.cpp').write_text('\n'.join(f'int row_{i} = {i};' for i in range(1, 6001)) + '\n')
(repo / 'long.txt').write_text('é' * 200000 + ' END\ntail')
git('add', '.')
git('commit', '-qm', 'Keyboard baseline')
lines[0] = lines[0].replace('alpha', 'delta')
lines[39] = 'int line_40 = 400;'
if args.offset_context:
    lines.insert(4, 'int inserted = 5;')
(repo / 'a.cpp').write_text('\n'.join(lines) + '\n')
before = git('diff')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def settle():
    return 'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 12\n'

def capture(label, count):
    return settle() + f'workspace_checkpoint {count} {label}\nscreenshot {label}\n'

def open_file(path):
    return f'key CMD+P\nwait_for_refresh\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nkey ENTER\n' + settle()

for zoom in args.zooms:
    for mode in args.modes:
        directory = out / f'{zoom}-{mode}'
        directory.mkdir()
        count = 2 if mode == 'source' else 1
        setup = 'resize 1800 1400\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        setup += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:a.cpp\n'
        if mode == 'source':
            setup += open_file('a.cpp:1:1')
        else:
            if mode.startswith('split'):
                setup += 'click_text "Split"\n'
            setup += settle() + 'click_ui hunk_header_label\nkey CTRL+G\nwait_frames 4\nclick_ui line_picker_input\ntype "1"\nkey ENTER\n'
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
        def workspace(label):
            return json.loads((directory / f'{label}.workspace.json').read_text())
        def caret(label):
            value = workspace(label)
            return next(t['caret'] for t in value['tabs'] if t['id'] == value['active'])
        def point(label, line, column):
            value = caret(label)
            assert value['side'] == ('before' if mode.endswith('before') else 'after')
            assert value['line'] == line and value['column'] == column, (zoom, mode, label, value, line, column)
        replay('probe', setup)
        initial = layout('initial')
        side = (1 if mode.endswith('before') else 2) if mode.startswith('split') else 0
        sign = '-' if mode.endswith('before') else '+' if mode != 'source' else ' '
        row = next(r for r in initial['reading_rows'] if r['line'] == 1 and r['side'] == side and r['sign'] == sign)
        x, y = row['content_x'] + .1, row['rect']['y'] + row['rect']['height'] / 2
        script = setup + f'drag_to {x} {y} {x} {y}\n' + capture('clicked', count)
        script += 'key RIGHT\n' + capture('right', count)
        if args.baseline:
            replay('baseline', script)
            assert caret('right') == caret('clicked')
            print(f'PASS baseline {zoom}% {mode}: arrow does not move caret', flush=True)
            continue
        script += 'key SHIFT+RIGHT\n' + capture('extended', count)
        script += 'key LEFT\n' + capture('collapsed', count)
        script += 'key CMD+RIGHT\n' + capture('line_end', count)
        script += 'key ALT+LEFT\n' + capture('word_left', count)
        script += 'key SHIFT+ALT+LEFT\n' + capture('word_extended', count)
        script += 'key HOME\nkey DOWN\nkey RIGHT\n' + capture('composed', count)
        script += 'key RIGHT\n' + capture('emoji', count)
        script += 'key SHIFT+RIGHT\n' + capture('context_extended', count)
        script += 'key END\nkey UP\n' + capture('up', count)
        script += 'key CMD+DOWN\n' + capture('document_end', count)
        script += 'key SHIFT+LEFT\n' + capture('end_selected', count) + 'key RIGHT\n'
        if mode == 'source':
            script += 'click_ui full_file_options\n' + capture('bookmark_target', count) + 'key ESCAPE\n'
        script += 'key CMD+UP\n' + capture('document_start', count)
        script += 'key CMD+F\nwait_frames 4\ntype "beta"\n' + capture('find', count)
        script += 'key LEFT\n' + capture('find_left', count)
        script += 'key ESCAPE\nkey DELETE\nkey BACKSPACE\n' + capture('readonly', count)
        if mode == 'source':
            script += open_file('large.cpp') + 'click_ui diff_line\nkey CMD+DOWN\n' + capture('large_end', 3)
            script += 'key CMD+UP\n' + capture('large_start', 3)
            script += 'key CTRL+G\nwait_frames 4\nclick_ui line_picker_input\ntype "4096"\nkey ENTER\n' + settle()
            script += 'focus_ui diff_scroll\nkey CMD+RIGHT\nkey RIGHT\n' + capture('next_page', 3)
            script += 'key LEFT\n' + capture('previous_page', 3)
            script += open_file('long.txt') + 'click_ui diff_line\nkey CMD+RIGHT\n' + capture('long_end', 4)
            script += 'key CMD+LEFT\n' + capture('long_start', 4)
        script += 'bench_frames 120\nexpect_p99_below 20\n'
        replay('journey', script)
        point('clicked', 1, 1)
        point('right', 1, 2)
        point('extended', 1, 3)
        assert layout('extended')['selection_text'] == 'λ'
        point('collapsed', 1, 2)
        assert layout('collapsed')['selection_text'] == ''
        point('line_end', 1, len(lines[0]) + 1)
        point('word_left', 1, 15)
        assert layout('word_extended')['selection_text'] == 'beta\t'
        point('composed', 2, 3)
        point('emoji', 2, 4)
        assert layout('context_extended')['selection_text'] == ' ', (zoom, mode, layout('context_extended')['selection_text'])
        assert caret('up')['line'] == 1
        end_line = len(lines) if mode == 'source' else 43 + (int(args.offset_context) if not mode.endswith('before') else 0)
        end_text = lines[-1] if mode == 'source' else 'int line_43 = 43;'
        point('document_end', end_line, len(end_text) + 1)
        assert layout('end_selected')['selection_text'] == ';'
        assert layout('end_selected')['selection_location'] == f'a.cpp:L{end_line}\n;'
        if args.offset_context and mode == 'unified_before':
            assert any(r['line'] == 44 and r['sign'] == ' ' for r in layout('end_selected')['reading_rows'])
        point('document_start', 1, 1)
        assert caret('find_left') == caret('find')
        history_size = len(workspace('clicked')['history'])
        for label in ['right', 'extended', 'collapsed', 'line_end', 'word_left', 'word_extended', 'composed', 'emoji', 'up', 'document_end', 'document_start']:
            assert len(workspace(label)['history']) == history_size, (zoom, mode, label, 'movement appended history')
            focused = [n for n in layout(label)['nodes'] if n.get('visual_focus')]
            assert focused and focused[0]['focus_target']['region'] == 'Code', (zoom, mode, label, focused)
        for label in ['right', 'extended', 'document_end', 'document_start']:
            value = layout(label)
            carets = [n for n in value['nodes'] if n.get('name') == 'code_caret' and n['rendered'] and n['visible_rect']['height'] > 0]
            assert len(carets) == 1, (zoom, mode, label, carets)
        def y_of(label):
            return next(r['rect']['y'] for r in layout(label)['reading_rows'] if r['line'] == 1 and r['side'] == side and r['sign'] == sign)
        assert abs(y_of('clicked') - y_of('extended')) < 1
        if mode == 'source':
            assert any(n.get('name') == f'context_menu_item_Bookmark line {len(lines)}' and n['rendered'] for n in layout('bookmark_target')['nodes'])
            point('large_end', 6000, len('int row_6000 = 6000;') + 1)
            point('large_start', 1, 1)
            point('next_page', 4097, 1)
            point('previous_page', 4096, len('int row_4096 = 4096;') + 1)
            point('long_end', 1, 200005)
            point('long_start', 1, 1)
        assert git('diff') == before and git('diff', '--cached') == ''
        print(f'PASS {zoom}% {mode}: graphemes, selection, words, boundaries, focus, history and read-only contents', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, offset_context=args.offset_context, binary_sha256=digest), indent=2) + '\n')
