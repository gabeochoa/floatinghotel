import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
real_git = shutil.which('git')


def git(*args, **kwargs):
    return subprocess.check_output([real_git, '-C', str(repo), *args], **kwargs)


git('init', '-q', '-b', 'main')
stream = bytearray()
for i in range(240):
    title = f'History entry {i:03}\n'.encode()
    content = f'int revision = {i};\n'.encode()
    stream.extend(f'commit refs/heads/main\nmark :{i + 1}\ncommitter History <history@example.invalid> {1700000000 + i} +0000\ndata {len(title)}\n'.encode())
    stream.extend(title)
    if i:
        stream.extend(f'from :{i}\n'.encode())
    stream.extend(f'M 100644 inline reader.cpp\ndata {len(content)}\n'.encode())
    stream.extend(content)
    stream.extend(b'\n')
git('fast-import', '--quiet', input=bytes(stream))
git('reset', '--hard', '-q', 'HEAD')
hashes = git('log', '--topo-order', '--format=%H').decode().splitlines()
wrapper_dir = out / 'bin'
wrapper_dir.mkdir()
wrapper = wrapper_dir / 'git'
wrapper.write_text(f'''#!{sys.executable}
import json, os, pathlib, sys, time
args = sys.argv[1:]
skip = next((a for a in args if a.startswith('--skip=')), None)
if skip:
    directory = pathlib.Path(os.environ['FH_PAGE_TEST'])
    with (directory / 'requests.jsonl').open('a') as log:
        log.write(json.dumps(args) + '\\n')
    failed = directory / 'failed_once'
    if skip == '--skip=100' and not failed.exists():
        failed.touch()
        sys.stderr.write('Injected history page failure\\n')
        sys.exit(1)
    time.sleep(1)
os.execv({real_git!r}, [{real_git!r}, *args])
''')
wrapper.chmod(0o755)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
geometry = {}
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()

    def capture(name, count=2, settle=True):
        return ('wait_for_refresh\n' if settle else '') + f'wait_frames 4\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'

    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "History entry 239"\n' + capture('initial')
    script += 'hover_ui commit_log_scroll\nscroll_wheel 0 -1000\nwait_for_refresh\nwait_frames 8\nclick_text "History entry 140"\nhover_ui history_branch\n' + capture('error')
    script += ('key TAB\n' + capture('retry_focus') + 'key ENTER\n' if zoom == 140 else 'click_ui lazy_load\n') + capture('loading', settle=False)
    script += capture('appended')
    if zoom == 140:
        script += 'key UP\n' + capture('retry_return') + 'key DOWN\n' + capture('retry_back')
    script += 'hover_ui commit_log_scroll\nscroll_wheel 0 -1000\n' + capture('loading_second', settle=False)
    script += capture('complete')
    script += 'hover_ui commit_log_scroll\nscroll_wheel 0 -1000\nwait_frames 8\nclick_text "History entry 000"\n' + capture('root')
    script += 'key DOWN\n' + capture('endpoint')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    environment = dict(os.environ, FH_NATIVE_MENUS='1', FH_PAGE_TEST=str(directory), PATH=str(wrapper_dir) + os.pathsep + os.environ['PATH'])
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'

    def state(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())

    def layout(name):
        return json.loads((directory / f'{name}.json').read_text())

    def active(name):
        current = state(name)
        return next(t for t in current['tabs'] if t['id'] == current['active'])

    if zoom == 140:
        focused = [n for n in layout('retry_focus')['nodes'] if n['focused'] and n['rendered']]
        assert len(focused) == 1 and focused[0]['focus_target']['control'] == 'lazy_load', focused
    retry = next(n for n in layout('retry_focus' if zoom == 140 else 'error')['nodes'] if n['rendered'] and n.get('name') == 'lazy_load')
    def luminance(rgb):
        channels = [value / 255 for value in rgb[:3]]
        linear = [value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4 for value in channels]
        return sum(value * weight for value, weight in zip(linear, [.2126, .7152, .0722]))
    contrast = (luminance(retry['text_color']) + .05) / (luminance(retry['background']) + .05)
    assert contrast >= 4.5, (zoom, contrast, retry)
    assert retry['measured_text_width'] <= retry['rect']['width'] - retry['padding']['left'] - retry['padding']['right'], (zoom, retry)
    assert state('initial')['commit_log']['hashes'] == hashes[:100], zoom
    assert 'Injected history page failure' in state('error')['commit_log']['error'], zoom
    assert state('error')['commit_log']['hashes'] == hashes[:100], zoom
    for name, count in [('loading', 100), ('appended', 200), ('loading_second', 200), ('complete', 240)]:
        log = state(name)['commit_log']
        assert log['count'] == count and log['hashes'] == hashes[:count], (zoom, name, log)
        assert log['loading'] == name.startswith('loading') and not log['error'], (zoom, name, log)
        assert log['has_more'] == (count < 240), (zoom, name)
        assert active(name)['revision'] == hashes[99], (zoom, name, active(name))
        assert state(name)['history_index'] == state('error')['history_index'] + (2 if zoom == 140 and name in ['loading_second', 'complete'] else 0), (zoom, name)
    if zoom == 140:
        assert active('retry_return')['revision'] == hashes[98], active('retry_return')
        assert active('retry_back')['revision'] == hashes[99], active('retry_back')
    geometry[str(zoom)] = {'retry_label_contrast': contrast}
    for before, after in [('error', 'loading'), ('loading', 'appended'), ('loading_second', 'complete')]:
        def rows(name):
            return {n['focus_target']['item']: n for n in layout(name)['nodes']
                    if n['rendered'] and n.get('name') == 'commit_row' and n['visible_rect']['height'] > 0}
        old, new = rows(before), rows(after)
        assert old and old.keys() <= new.keys(), (zoom, before, old.keys(), new.keys())
        maximum = max(abs(row['rect']['y'] - new[key]['rect']['y']) for key, row in old.items())
        assert maximum < 1, (zoom, before, maximum)
        geometry[str(zoom)][before] = {'rows': len(old), 'maximum_y_change': maximum}
    assert active('root')['revision'] == active('endpoint')['revision'] == hashes[-1], zoom
    assert state('root')['history_index'] == state('endpoint')['history_index'], zoom
    assert not any(n['rendered'] and n.get('name') == 'lazy_load' for n in layout('root')['nodes']), zoom
    requests = [json.loads(line) for line in (directory / 'requests.jsonl').read_text().splitlines()]
    assert [next(a for a in args if a.startswith('--skip=')) for args in requests] == ['--skip=100', '--skip=100', '--skip=200'], requests
    assert all(hashes[0] in args for args in requests), requests
    assert git('status', '--porcelain') == b'' and git('log', '--format=%H').decode().splitlines() == hashes
    print(f'PASS {zoom}%: page failure/retry, stationary rows, pinned history, repeated append, root endpoint', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=geometry), indent=2) + '\n')
