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

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], stderr=subprocess.STDOUT).decode().strip()

git('init', '-q', '-b', 'main')
git('config', 'user.name', 'A very long author name with café and many extra words for narrow metadata')
git('config', 'user.email', 'long-author-address@example.invalid')
git('config', 'commit.gpgsign', 'false')
(repo / 'code.txt').write_text('File beneath the full message\n')
git('add', '.')
git('commit', '-qm', 'Short root')
git('checkout', '-qb', 'other')
(repo / 'other.txt').write_text('Other branch\n')
git('add', '.')
git('commit', '-qm', 'Other parent')
other = git('rev-parse', 'HEAD')
git('checkout', '-q', 'main')
(repo / 'code.txt').write_text('File beneath the full message\nChanged code\n')
git('add', '.')
subject = 'Long metadata subject ' + 'café words to wrap safely ' * 32 + 'SUBJECT_END'
git('commit', '-qm', subject + '\n\n' + ''.join(f'Message line {i}\n' for i in range(1, 10001)) + 'FULL_MESSAGE_END')
long_hash = git('rev-parse', 'HEAD')
git('merge', '--no-ff', '-qm', 'Merge metadata fixture', 'other')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def capture(name, count=2):
    return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Long metadata subject"\nwait_for_refresh\nkey ENTER\n' + capture('compact')
    script += 'resize 1150 850\nwait_frames 10\nhover_ui commit_detail_scroll\nscroll_wheel 0 20000\nwait_frames 30\n' + capture('narrow')
    if not args.baseline:
        script += 'click_ui commit_meta_toggle\n' + capture('details_narrow')
        script += 'validate message_rows_bounded=true\nresize 1600 1100\n' + capture('details')
        if zoom == 100:
            script += 'expect_text SUBJECT_END\n'
        script += 'bench_frames 120\nexpect_p99_below 20\n'
        script += 'hover_ui commit_details_scroll\nscroll_wheel 0 -20000\nwait_frames 30\n' + capture('end')
        script += 'expect_text FULL_MESSAGE_END\nvalidate message_rows_bounded=true\n'
        script += 'scroll_wheel 0 20000\nwait_frames 30\n' + capture('top')
        script += 'key ESCAPE\nwait_frames 10\n' + capture('dismissed')
        script += 'click_text "Short root"\nwait_for_refresh\nkey ENTER\n' + capture('root', 3)
        script += 'click_text "Long metadata subject"\nwait_for_refresh\nwait_frames 10\nhover_ui commit_detail_scroll\nscroll_wheel 0 20000\nwait_frames 30\n' + capture('retained', 3)
        script += 'click_ui commit_meta_toggle\n' + capture('reopened', 3)
        script += 'key ESCAPE\nwait_frames 10\n' + capture('collapsed', 3)
        script += 'click_text "Merge metadata fixture"\n' + capture('merge', 4)
        script += 'click_ui merge_parent_select\nwait_frames 3\nclick_ui "context_menu_item_Parent 2 · ' + other[:12] + '"\n' + capture('parent2', 4)
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
    def nodes(name, debug):
        return [n for n in layout(name)['nodes'] if n.get('name') == debug and n['rendered']]
    def node(name, debug):
        return nodes(name, debug)[0]
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def active(name):
        value = state(name)
        return next(t for t in value['tabs'] if t['id'] == value['active'])
    for name in ['compact', 'narrow']:
        assert node(name, 'commit_heading')['rect']['height'] <= 60.1 * zoom / 100
        assert node(name, 'commit_detail_subject')['font_size']['value'] == 20
        assert not nodes(name, 'commit_meta_box')
        if not args.baseline:
            row = node(name, 'commit_meta_compact')['rect']
            assert abs(row['height'] - 32 * zoom / 100) < 1
            for debug in ['commit_author', 'commit_relative_date', 'commit_meta_toggle']:
                rect = node(name, debug)['rect']
                assert rect['x'] >= row['x'] and rect['x'] + rect['width'] <= row['x'] + row['width'] + 1, (zoom, name, debug, row, rect)
            assert not nodes(name, 'commit_message_toggle')
    if not args.baseline:
        assert active('details')['details_expanded'] and active('reopened')['details_expanded']
        assert not active('retained')['details_expanded'] and not active('dismissed')['details_expanded']
        assert not active('root')['details_expanded'] and not active('collapsed')['details_expanded']
        assert not active('merge')['details_expanded'] and nodes('merge', 'merge_parent_select')
        assert other in json.dumps(active('parent2')), active('parent2')
        assert node('end', 'commit_heading')['rect'] == node('details', 'commit_heading')['rect']
        assert node('end', 'commit_meta_compact')['rect'] == node('details', 'commit_meta_compact')['rect']
        assert node('dismissed', 'commit_detail_scroll')['scroll'] == node('top', 'commit_detail_scroll')['scroll']
        for name in ['details_narrow', 'details']:
            box = node(name, 'commit_meta_box')['rect']
            for value in nodes(name, 'meta_value'):
                rect = value['rect']
                assert rect['x'] >= box['x'] and rect['x'] + rect['width'] <= box['x'] + box['width'] + 1, (zoom, name, rect, box)
            assert len(nodes(name, 'commit_body_line')) < 300
            button = node(name, 'commit_meta_toggle')['rect']
            viewport = node(name, 'commit_detail_scroll')['rect']
            assert button['x'] + button['width'] <= viewport['x'] + viewport['width'] - 7 * zoom / 100
    print(f'PASS {zoom}% commit metadata' + (' baseline' if args.baseline else ', disclosure, retained state, bounded message and merge parents'), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
assert git('status', '--porcelain') == ''
(out / 'result.json').write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest), indent=2) + '\n')
