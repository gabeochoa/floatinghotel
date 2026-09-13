import argparse
import hashlib
import json
import os
from pathlib import Path
import re
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
for name in ['a.cpp', 'b.txt']:
    (repo / name).write_text('NEEDLE alpha\nOLD alpha\nNEW alpha\n')
(repo / 'many.cpp').write_text('LIMIT match\n' * 6000)
for args in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Debounce fixture'),
             ('config', 'user.email', 'debounce@example.invalid'), ('config', 'commit.gpgsign', 'false'),
             ('add', '.'), ('commit', '-qm', 'Search fixture')]:
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
wrapper = out / 'bin'
wrapper.mkdir()
real_git = shutil.which('git')
(wrapper / 'git').write_text('#!/usr/bin/env python3\nimport json, os, sys, time\n'
    + "if 'grep' in sys.argv:\n"
    + "    query = sys.argv[sys.argv.index('-e') + 1]\n"
    + "    def record(event):\n"
    + "        with open(os.environ['FH_SEARCH_LOG'], 'a') as log: log.write(json.dumps(dict(event=event, query=query, time=time.monotonic(), pid=os.getpid())) + '\\n')\n"
    + "    record('start')\n"
    + "    if query == 'OLD':\n"
    + "        open(os.environ['FH_SEARCH_LOG'] + '.started', 'w').close()\n"
    + "        time.sleep(2); record('delay_finished')\n"
    + f"os.execv({real_git!r}, [{real_git!r}, *sys.argv[1:]])\n")
(wrapper / 'git').chmod(0o755)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
started_cancellations = 0
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    def capture(name, wait=True):
        return ('wait_for_refresh\nwait_frames 10\n' if wait else '') + f'workspace_checkpoint 1 {name}\nscreenshot {name}\n'
    def query(text):
        return f'click_ui repo_search_input\nkey CMD+A\ntype "{text}"\n'
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'key CMD+SHIFT+F\nwait_frames 3\nclick_ui repo_search_input\n'
    script += ''.join(f'type "{letter}"\n' for letter in 'NEEDLE') + capture('automatic')
    script += query('OLD') + 'key ENTER\nwait_for_path searches.jsonl.started\n' + capture('older_loading', False)
    script += query('NEW') + capture('newer')
    script += 'wait_frames 700\n' + capture('after_old')
    script += 'click_ui repo_search_options\nclick_ui repo_search_include\ntype "*.cpp"\n' + capture('filtered')
    script += 'click_ui repo_search_options\n' + query('LIMIT') + 'key ENTER\n' + capture('limited')
    script += 'click_ui repo_search_input\nkey CMD+A\nkey BACKSPACE\n' + capture('empty')
    script += query('NEEDLE') + 'click_ui repo_search_close\n' + capture('closed', False)
    script += 'key CMD+SHIFT+F\n' + capture('reopened')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TRACE_READING='1', PATH=str(wrapper) + ':' + os.environ['PATH'],
                     FH_SEARCH_LOG=str(directory / 'searches.jsonl')), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, (zoom, result.returncode, directory / 'run.log')
    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())['search']
    def nodes(name):
        return json.loads((directory / f'{name}.json').read_text())['nodes']
    assert state('automatic')['matches'] == 2 and state('automatic')['submissions'] == 1, state('automatic')
    assert state('older_loading')['loading'] and state('older_loading')['matches'] == 0, state('older_loading')
    assert state('newer')['submitted_query'] == 'NEW' and state('newer')['matches'] == 2, state('newer')
    assert state('after_old') == state('newer')
    assert state('filtered')['matches'] == 1 and state('filtered')['include'] == '*.cpp', state('filtered')
    assert state('limited')['matches'] == 5000 and state('limited')['truncated'], state('limited')
    assert state('limited')['captured_bytes'] <= 4 * 1024 * 1024
    assert state('empty')['matches'] == 0 and not state('empty')['loading'] and not state('empty')['pending'], state('empty')
    assert not state('closed')['open']
    assert state('reopened')['matches'] == 1 and state('reopened')['query'] == 'NEEDLE', state('reopened')
    for name in ['automatic', 'newer', 'limited', 'empty']:
        rendered = nodes(name)
        for control in ['repo_search_input', 'repo_search_status', 'repo_search_results']:
            node = next(n for n in rendered if n.get('name') == control)
            assert node['visible_rect']['height'] >= node['rect']['height'] - 1, (zoom, name, control)
        assert sum(n.get('name') == 'repo_search_result' for n in rendered) < 50
    for name in ['newer', 'after_old']:
        assert all('NEW alpha' in n['text'] for n in nodes(name) if n.get('name') == 'repo_search_result')
    events = [json.loads(line) for line in (directory / 'searches.jsonl').read_text().splitlines()]
    starts = [e['query'] for e in events if e['event'] == 'start']
    assert [query for query in starts if query != 'OLD'] == ['NEEDLE', 'NEW', 'NEW', 'LIMIT', 'NEEDLE'], starts
    assert starts.count('OLD') <= 1, starts
    old = next((e for e in events if e['query'] == 'OLD'), None)
    if old:
        started_cancellations += 1
        time.sleep(max(0, old['time'] + 2.1 - time.monotonic()))
        try:
            os.kill(old['pid'], 0)
            raise AssertionError('Superseded Git worker remains alive')
        except ProcessLookupError:
            pass
        events = [json.loads(line) for line in (directory / 'searches.jsonl').read_text().splitlines()]
        assert not any(e['event'] == 'delay_finished' for e in events), events
    traces = re.findall(r'search_submit: reason=(\w+) delay_ms=([\d.]+) query=(.*)', (directory / 'run.log').read_text())
    assert traces and all(float(delay) >= 150 for reason, delay, _ in traces if reason == 'pause'), traces
    assert any(reason == 'explicit' and query == 'OLD' and float(delay) < 150 for reason, delay, query in traces), traces
    assert subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain']) == b''
    print(f'PASS {zoom}%: rapid edits coalesce after 150 ms, Enter immediate, old worker cancelled, filters debounce, 5000-result cap, empty/closed queries', flush=True)
assert started_cancellations > 0
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
