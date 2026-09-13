import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()


def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()


def write(version):
    (repo / 'sample.txt').write_text(''.join(f'{version}_{i:03d}\n' for i in range(1, 101)))


git('init', '-q', '-b', 'main')
git('config', 'user.name', 'Bookmark fixture')
git('config', 'user.email', 'bookmark@example.invalid')
git('config', 'commit.gpgsign', 'false')
write('old')
git('add', '.')
git('commit', '-qm', 'Bookmark original')
old = git('rev-parse', 'HEAD')
write('head')
git('commit', '-qam', 'Bookmark current')
write('index')
git('add', '.')
write('working')
before = (git('diff'), git('diff', '--cached'))
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name, count):
    return f'wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


def menu(label):
    return f'click_ui full_file_options\nwait_frames 3\nclick_ui "context_menu_item_{label}"\nwait_frames 3\n'


def picker(path, working=False):
    scope = 'click_ui file_picker_working_scope\nwait_for_refresh\n' if working else ''
    return f'key CMD+P\nwait_for_refresh\n{scope}click_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\nkey ENTER\nwait_for_refresh\nwait_frames 10\n'


for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'
    settings.mkdir()
    setup = 'resize 1150 850\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    setup += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)

    def replay(name, script):
        target = directory / name
        target.mkdir()
        path = target / 'journey.e2e'
        path.write_text(script)
        with (target / 'run.log').open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
                f'--screenshot-dir={target}', '--e2e-timeout=180'], cwd=ROOT,
                env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings)),
                stdout=log, stderr=subprocess.STDOUT, timeout=210)
        assert result.returncode == 0, (zoom, target / 'run.log')
        return target

    script = setup + 'click_text "Bookmark original"\nwait_for_refresh\nkey ENTER\n'
    script += picker('sample.txt:4') + capture('before', 3)
    script += menu('Bookmarks') + 'expect_text "No bookmarks"\nkey ESCAPE\n'
    script += menu('Bookmark line 4') + capture('historical_added', 3)
    script += picker('sample.txt:2', working=True) + menu('Bookmark line 2') + capture('working_added', 4)
    script += 'click_ui review_staged_changes\nwait_for_refresh\n' + picker('sample.txt:3')
    script += capture('index_source', 6) + menu('Bookmark line 3') + capture('index_added', 6)
    script += menu('Bookmarks') + capture('navigator', 6) + 'key ESCAPE\n' + capture('dismissed', 6)
    script += 'key ENTER\nwait_frames 3\nkey DOWN\nkey DOWN\nkey DOWN\nkey ENTER\nwait_frames 3\nkey DOWN\nkey ENTER\n'
    script += capture('keyboard_historical', 6) + 'expect_text "old_004"\nkey CMD+W\nwait_frames 5\nclick_ui content_document_4\n'
    script += capture('closed', 5) + menu('Bookmarks') + f'click_ui "context_menu_item_sample.txt:L4 @ {old[:7]}"\n'
    script += capture('reopened', 6) + 'expect_text "old_004"\n'
    script += menu('Bookmarks') + 'click_ui "context_menu_item_sample.txt:L2 @ Working tree"\n' + capture('working_return', 6)
    script += 'expect_text "working_002"\n' + menu('Bookmarks') + 'click_ui "context_menu_item_sample.txt:L3 @ Index"\n'
    script += capture('index_return', 6) + 'expect_text "index_003"\nsave_window_state\nbench_frames 120\nexpect_p99_below 20\n'
    target = replay('journey', script)

    def layout(name, root=target):
        return json.loads((root / f'{name}.json').read_text())

    def state(name, root=target):
        return json.loads((root / f'{name}.workspace.json').read_text())

    def active(name, root=target):
        value = state(name, root)
        return next(t for t in value['tabs'] if t['id'] == value['active'])

    def node(name, debug, root=target):
        return next(n for n in layout(name, root)['nodes'] if n.get('name') == debug and n['rendered'])

    assert node('before', 'diff_scroll')['rect'] == node('historical_added', 'diff_scroll')['rect']
    assert layout('before')['reading_rows'] == layout('historical_added')['reading_rows']
    assert layout('index_added')['reading_rows'] == layout('dismissed')['reading_rows']
    for name, revision, line in [('keyboard_historical', old, 4), ('reopened', old, 4), ('working_return', '', 2), ('index_return', 'INDEX', 3)]:
        assert active(name)['revision'] == revision, (zoom, name, active(name))
        visit = state(name)['history'][state(name)['history_index']]['location']['source']
        assert visit['line'] == line, (zoom, name, visit)
    for name in ['historical_added', 'working_added', 'index_added', 'navigator']:
        assert not any(n.get('name') == 'bookmarks_row' and n['rendered'] for n in layout(name)['nodes'])
    assert not any(n.get('visual_focus') for n in layout('navigator')['nodes'] if n['rendered'] and n.get('name', '').startswith('context_menu_item_'))
    saved = json.loads((settings / 'settings.json').read_text())
    bookmarks = saved['code_bookmarks'][str(repo)]
    assert {(b['revision'], b['line']) for b in bookmarks} == {(old, 4), ('', 2), ('INDEX', 3)}, bookmarks
    (directory / 'saved-bookmarks.json').write_text(json.dumps(bookmarks, indent=2) + '\n')
    bookmarks += [dict(path='sample.txt', revision='', line=i, label='') for i in range(10, 50)]
    bookmarks += [dict(path='sample.txt', revision='1' * 40, line=1, label='Missing object')]
    (settings / 'settings.json').write_text(json.dumps(dict(code_bookmarks={str(repo): bookmarks})))
    script = setup + picker('sample.txt') + capture('before_menu', 2) + menu('Bookmarks') + capture('long_menu', 2)
    script += 'key DOWN\n' * (len(bookmarks) - 1) + capture('last_visible', 2) + 'key ENTER\n' + capture('last_open', 2)
    script += 'expect_text "working_049"\n' + menu('Bookmarks') + 'key UP\nkey ENTER\n'
    script += capture('missing', 3) + 'assert_ui full_file_error hidden=false\n'
    long = replay('long', script)
    popup = node('last_visible', 'context_menu', long)['rect']
    assert popup['y'] >= 0 and popup['y'] + popup['height'] <= 850
    entries = [n for n in layout('last_visible', long)['nodes'] if n.get('name', '').startswith('context_menu_item_') and n['rendered']]
    assert len(entries) < len(bookmarks)
    assert any('sample.txt:L49 @ Working tree' in n.get('text', '') for n in entries)
    assert active('last_open', long)['revision'] == ''
    assert active('missing', long)['revision'] == '1' * 40
    assert not layout('missing', long)['reading_rows']
    assert (git('diff'), git('diff', '--cached')) == before
    print(f'PASS {zoom}%: unchanged viewport, three revisions, keyboard navigation, closed tab, saved bookmarks, bounded list and missing object', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
