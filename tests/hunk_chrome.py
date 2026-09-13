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
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True)

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Hunk fixture'),
                ('config', 'user.email', 'hunk@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
(repo / 'sample.cpp').write_text(''.join(f'int value_{i} = {i};\n' for i in range(1, 101)))
git('add', '.')
git('commit', '-qm', 'Hunk control fixture')
p = repo / 'sample.cpp'
p.write_text(p.read_text().replace('value_10 = 10', 'value_10 = 1000').replace('value_80 = 80', 'value_80 = 8000'))
git('add', '.')
git('commit', '-qm', 'Two commit hunks')
p.write_text(p.read_text().replace('value_10 = 1000', 'value_10 = 1001').replace('value_80 = 8000', 'value_80 = 8001'))
before = git('diff')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def capture(name):
    return f'wait_frames 12\nscreenshot {name}\n'

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui jump_to_diff:sample.cpp\nmouse_move 1 1\n' + capture('idle')
    script += 'hover_ui hunk_header_row\n' + capture('hover')
    script += 'mouse_move 1 1\n' + capture('left')
    script += 'click_ui hunk_header_label\nmouse_move 1 1\n' + capture('focused')
    script += 'key TAB\n' + capture('keyboard')
    script += 'right_click_ui hunk_header_row\n' + capture('menu')
    script += 'expect_text "Copy hunk"\nexpect_text "Show surrounding lines"\nexpect_text "Approve hunk"\nexpect_text "Comment on hunk"\nkey ESCAPE\n'
    script += 'resize 1150 850\nwait_frames 10\nclick_ui jump_to_diff:sample.cpp\nhover_ui diff_scroll\nscroll_wheel 0 20000\nwait_frames 15\nmouse_move 1 1\n' + capture('narrow_idle')
    script += 'hover_ui hunk_header_row\n' + capture('narrow_hover')
    if not args.baseline:
        script += 'right_click_ui hunk_header_row\nwait_frames 3\nclick_ui "context_menu_item_Comment on hunk"\nwait_frames 3\nclick_ui comment_input\ntype "Hunk feedback"\nclick_ui comment_add_btn\n' + capture('commented')
        script += 'expect_text "commented · click to expand"\nbench_frames 120\nexpect_p99_below 20\n'
        script += 'resize 1600 1100\nclick_text "Two commit hunks"\nwait_for_refresh\nwait_frames 12\nmouse_move 1 1\n' + capture('commit_idle')
        script += 'hover_ui hunk_header_row\n' + capture('commit_hover')
        script += 'click_ui hunk_header_label\nmouse_move 1 1\n' + capture('commit_focus')
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, (zoom, directory / 'run.log')
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def nodes(name, debug):
        return [n for n in layout(name)['nodes'] if n.get('name') == debug and n['rendered'] and n['visible_rect']['height'] > 0]
    if not args.baseline:
        for idle, hover in [('idle', 'hover'), ('left', 'hover'), ('narrow_idle', 'narrow_hover'), ('commit_idle', 'commit_hover')]:
            before_nodes = nodes(idle, 'hunk_header_btns')
            after_nodes = nodes(hover, 'hunk_header_btns')
            assert before_nodes and after_nodes
            assert before_nodes[0]['opacity'] == 0 and after_nodes[0]['opacity'] == 1, (zoom, idle, before_nodes[0])
            assert nodes(idle, 'hunk_header_label')[0]['rect'] == nodes(hover, 'hunk_header_label')[0]['rect']
            assert before_nodes[0]['rect'] == after_nodes[0]['rect']
            caption = nodes(hover, 'hunk_header_label')[0]['rect']
            assert caption['width'] >= 140 * zoom / 100, (zoom, hover, caption)
        for name in ['hover', 'narrow_hover']:
            for debug in ['expand_diff_context', 'approve_hunk_btn', 'comment_hunk_btn']:
                action = nodes(name, debug)[0]
                assert abs(action['rect']['width'] - action['visible_rect']['width']) < .1, (zoom, name, debug)
                assert abs(action['rect']['height'] - action['visible_rect']['height']) < .1, (zoom, name, debug)
        for name in ['hover', 'narrow_hover']:
            for icon_name, button_name in [('hunk_approve_icon', 'approve_hunk_btn'), ('hunk_comment_icon', 'comment_hunk_btn')]:
                icons = nodes(name, icon_name)
                if icons:
                    icon = icons[0]['rect']
                    button = nodes(name, button_name)[0]['rect']
                    assert abs(icon['x'] + icon['width'] / 2 - button['x'] - button['width'] / 2) < .1
                    assert icon['y'] >= button['y'] and icon['y'] + icon['height'] <= button['y'] + button['height'] + .1
        for name in ['focused', 'keyboard', 'commit_focus']:
            assert nodes(name, 'hunk_header_btns')[0]['opacity'] == 1
        for name in ['hover', 'commit_hover', 'commit_focus']:
            if len(nodes(name, 'hunk_header_btns')) > 1:
                assert nodes(name, 'hunk_header_btns')[1]['opacity'] == 0
    assert git('diff') == before and git('diff', '--cached') == ''
    print(f'PASS {zoom}% hunk controls' + (' baseline' if args.baseline else ', hover/focus, stable geometry, narrow location and context actions'), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest), indent=2) + '\n')
