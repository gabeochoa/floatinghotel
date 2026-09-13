import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
(repo / 'large.cpp').write_text(''.join(f'int {"OLD" if i == 4500 else "NEW" if i == 6200 else "value"}_{i} = {i};\n' for i in range(1, 6201)))
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Find cancellation'),
             ('config', 'user.email', 'find@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Find cancellation')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
blob = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD:large.cpp'], text=True).strip()
wrapper = out / 'bin'
wrapper.mkdir()
real_git = shutil.which('git')
(wrapper / 'git').write_text('#!/usr/bin/env python3\nimport json, os, sys, time\nfrom pathlib import Path\n'
    + "directory = Path(os.environ['FH_FIND_EVENTS'])\n"
    + f"if 'cat-file' in sys.argv and 'blob' in sys.argv and {blob!r} in sys.argv and (directory / 'arm').exists() and not (directory / 'started').exists():\n"
    + "    (directory / 'started').write_text(json.dumps(dict(pid=os.getpid(), time=time.monotonic())))\n"
    + "    time.sleep(2)\n"
    + "    (directory / 'delay_finished').touch()\n"
    + f"os.execv({real_git!r}, [{real_git!r}, *sys.argv[1:]])\n")
(wrapper / 'git').chmod(0o755)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, wait=True):
        return ('wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 10\n' if wait else '') + f'workspace_checkpoint 3 {name}\nscreenshot {name}\n'
    script = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Find cancellation"\nwait_for_refresh\nkey ENTER\nkey CMD+P\nwait_frames 3\ntype "large.cpp"\nwait_for_refresh\nkey ENTER\n' + capture('ready')
    script += f'key CMD+F\nwait_frames 3\ntouch_file ../{zoom}/arm\ntype "OLD"\nwait_for_path started\n' + capture('pending', False)
    script += 'click_ui diff_find_input\nkey CMD+A\ntype "NEW"\n' + capture('newer')
    script += 'wait_frames 700\n' + capture('after_old')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1', FH_FIND_EVENTS=str(directory), PATH=str(wrapper) + ':' + os.environ['PATH']),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, (zoom, result.returncode, directory / 'run.log')
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    assert state('pending')['source_find']['loading'] and state('pending')['source_find']['matches'] == 0
    for name in ['newer', 'after_old']:
        value = state(name)
        tab = next(t for t in value['tabs'] if t['id'] == value['active'])
        assert tab['find']['query'] == 'NEW' and tab['find']['position']['line'] == 6200, (zoom, name, tab)
        assert value['source_find']['matches'] == 1 and not value['source_find']['error']
        layout = json.loads((directory / (name + '.json')).read_text())
        assert any(r['line'] == 6200 and 'NEW' in r['text'] for r in layout['reading_rows'])
    started = json.loads((directory / 'started').read_text())
    time.sleep(max(0, started['time'] + 2.1 - time.monotonic()))
    assert not (directory / 'delay_finished').exists()
    try:
        os.kill(started['pid'], 0)
        raise AssertionError('Superseded source reader remains alive')
    except ProcessLookupError:
        pass
    print(f'PASS {zoom}%: running old source read cancelled, only the new query publishes and renders', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
