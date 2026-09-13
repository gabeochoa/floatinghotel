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
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
for name, text in [('a.cpp', 'int historical = 1;'), ('gone.cpp', 'int OLD_GONE = 1;'), ('old_name.cpp', 'int OLD_NAME = 1;')]:
    (repo / name).write_text(text + '\n')
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Scope fixture'),
             ('config', 'user.email', 'scope@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Original files')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
old = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
subprocess.run(['git', '-C', str(repo), 'mv', 'old_name.cpp', 'new_name.cpp'], check=True)
subprocess.run(['git', '-C', str(repo), 'rm', 'gone.cpp'], check=True, capture_output=True)
(repo / 'a.cpp').write_text('int committed = 2;\n')
subprocess.run(['git', '-C', str(repo), 'add', '.'], check=True)
subprocess.run(['git', '-C', str(repo), 'commit', '-qm', 'Current files'], check=True)
(repo / 'a.cpp').write_text('int INDEX_VALUE = 3;\n')
subprocess.run(['git', '-C', str(repo), 'add', 'a.cpp'], check=True)
(repo / 'a.cpp').write_text('int WORKING_VALUE = 4;\n')
(repo / 'untracked.cpp').write_text('int untracked = 5;\n')
status_before = subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain'])
index_before = subprocess.check_output(['git', '-C', str(repo), 'diff', '--cached'])
wrapper = out / 'bin'
wrapper.mkdir()
real_git = shutil.which('git')
(wrapper / 'git').write_text('#!/usr/bin/env python3\nimport os, sys, time, json\n'
    + f"log = {str(out / 'requests.jsonl')!r}\n"
    + "if 'ls-tree' in sys.argv and '-r' in sys.argv:\n"
    + "    with open(log, 'a') as f: f.write(json.dumps(sys.argv[1:]) + '\\n')\n"
    + "    time.sleep(0.6)\n"
    + f"os.execv({real_git!r}, [{real_git!r}, *sys.argv[1:]])\n")
(wrapper / 'git').chmod(0o755)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, count):
        return f'wait_for_refresh\nwait_frames 12\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    def query(text):
        return f'click_ui file_picker_input\nkey CMD+A\ntype "{text}"\nwait_frames 8\n'
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Original files"\nwait_for_refresh\nkey ENTER\nkey CMD+P\nwait_frames 3\nkey SHIFT+TAB\nwait_frames 3\nscreenshot pending_scope_focus\n'
    script += capture('loaded_scope_focus', 2)
    script += 'key ENTER\n' + capture('keyboard_working', 2)
    script += 'key ESCAPE\nwait_frames 3\nkey CMD+P\nwait_frames 3\nscreenshot historical_loading\n'
    script += 'click_ui file_picker_working_scope\n' + capture('working_after_cancel', 2)
    script += 'wait_frames 400\n' + capture('working_after_delay', 2)
    script += 'click_ui file_picker_document_scope\n' + capture('historical_catalog', 2)
    script += query('gone.cpp') + capture('historical_match', 2)
    script += 'key ENTER\n' + capture('historical_source', 3)
    script += 'key ALT+LEFT\nwait_for_refresh\nkey CMD+P\nwait_frames 3\nclick_ui file_picker_working_scope\n'
    script += query('a.cpp') + capture('working_match', 3)
    script += 'key ENTER\n' + capture('working_source', 4)
    script += 'click_ui review_staged_changes\nwait_for_refresh\nclick_ui content_document_5\nkey ENTER\nkey CMD+P\n'
    script += capture('index_match', 5)
    script += 'key ENTER\n' + capture('index_source', 6)
    script += 'key CMD+P\nwait_frames 3\nclick_ui file_picker_working_scope\n' + capture('index_to_working', 6)
    script += 'key ENTER\n' + capture('working_reused', 6)
    script += 'click_text "Original files"\nwait_for_refresh\nkey CMD+P\n' + query('old_name.cpp') + capture('old_name_match', 6)
    script += 'key ENTER\n' + capture('old_name_source', 7)
    script += 'key CMD+P\nwait_frames 3\nnew_tab\nwait_frames 400\nscreenshot other_repository\nclose_tab\nwait_for_refresh\n'
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1', PATH=str(wrapper) + os.pathsep + os.environ['PATH']),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    def results(name):
        return [n['text'] for n in layout(name)['nodes'] if n['rendered'] and n.get('name') == 'file_picker_result']
    for name in ['pending_scope_focus', 'loaded_scope_focus']:
        focused = [n for n in layout(name)['nodes'] if n['rendered'] and n['focused']]
        assert len(focused) == 1 and focused[0].get('name') == 'file_picker_working_scope', (zoom, name, focused)
    assert 'new_name.cpp' in results('keyboard_working') and 'gone.cpp' not in results('keyboard_working'), zoom
    assert results('historical_catalog') == ['a.cpp', 'gone.cpp', 'old_name.cpp'], zoom
    assert results('working_after_cancel') == results('working_after_delay'), zoom
    assert 'gone.cpp' not in results('working_after_delay') and 'new_name.cpp' in results('working_after_delay'), zoom
    assert results('historical_match') == ['gone.cpp'], zoom
    for name, path_name, revision, marker in [('historical_source', 'gone.cpp', old, 'OLD_GONE'),
            ('working_source', 'a.cpp', '', 'WORKING_VALUE'), ('index_source', 'a.cpp', 'INDEX', 'INDEX_VALUE'),
            ('old_name_source', 'old_name.cpp', old, 'OLD_NAME')]:
        snapshot = state(name)
        active = next(t for t in snapshot['tabs'] if t['id'] == snapshot['active'])
        assert active['path'] == path_name and active['revision'] == revision and not active['preview'], (zoom, name, active)
        assert any(marker in r['text'] for r in layout(name)['reading_rows']), (zoom, name)
    assert state('working_reused')['active'] == state('working_source')['active'], zoom
    assert not any(n['rendered'] and n.get('name') == 'file_picker_overlay' for n in layout('other_repository')['nodes']), zoom
    for name in ['historical_catalog', 'working_after_delay', 'index_match']:
        controls = [n for n in layout(name)['nodes'] if n['rendered'] and n.get('name') in ['file_picker_document_scope', 'file_picker_working_scope']]
        assert controls and all(n['visible_rect']['height'] >= n['rect']['height'] - 1 and n['visible_rect']['width'] >= n['rect']['width'] - 1 for n in controls), (zoom, name)
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == status_before
    assert subprocess.check_output(['git', '-C', str(repo), 'diff', '--cached']) == index_before
    print(f'PASS {zoom}%: historical deletion/rename, working and index contents, scope switch during delayed read, source reuse, repository switch', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, historical_revision=old), indent=2) + '\n')
