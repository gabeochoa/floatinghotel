import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

# Language-server navigation end to end with clangd: F12 from a call site in
# a.cpp opens math.h at the definition on line 120 (off screen, so a no-op
# cannot pass), Shift+F12 lists references, Alt+Shift+H lists callers,
# Alt+F12 shows the type, and a revision document is refused.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
args = parser.parse_args()
if not shutil.which('clangd'):
    raise SystemExit('SKIP clangd not installed')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*a):
    return subprocess.check_output(['git', '-C', str(repo), *a], text=True).strip()

(repo / 'math.h').write_text('#pragma once\n' + ''.join(f'// filler {i}\n' for i in range(2, 120)) +
                             'inline int add(int a, int b) { return a + b; }\n')
# Line 4 starts with the call, so Go to Line puts the caret on `add`.
(repo / 'a.cpp').write_text('#include "math.h"\n'
                            'int twice(int x) {\n'
                            '  return\n'
                            'add(x, x);\n'
                            '}\n'
                            'int main() { return twice(2) + add(1, 2); }\n')
(repo / 'compile_commands.json').write_text(json.dumps([{'directory': str(repo), 'file': 'a.cpp',
                                                         'arguments': ['clang++', '-std=c++17', '-c', 'a.cpp']}]))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Semantic fixture'),
                ('config', 'user.email', 'semantic@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Semantic fixture')]:
    git(*command)
commit = git('rev-parse', 'HEAD')

def caret_on_add():
    return ('hold_key 343\nkey P\nrelease_key 343\nwait_for_refresh\nclick_ui file_picker_input\nkey CMD+A\ntype "a.cpp"\n'
            'wait_frames 3\nwait_for_refresh\nkey ENTER\nwait_for_refresh\nwait_frames 10\n'
            'key CTRL+G\nwait_frames 4\nclick_ui line_picker_input\nkey CMD+A\ntype "4"\nkey ENTER\nwait_frames 10\n')

script = 'resize 1400 900\nwait_for_refresh\nwait_frames 10\n' + caret_on_add()
script += 'reading_probe definition source math.h - 120\nkey F12\nreading_checkpoint\nscreenshot definition\n'
script += 'key ALT+LEFT\nwait_for_refresh\nwait_frames 10\n'
script += caret_on_add() + 'hold_key 340\nkey F12\nrelease_key 340\nwait_frames 200\nexpect_text "3 references of add"\nscreenshot references\n'
script += 'click_ui repo_search_close\nwait_frames 3\n' + caret_on_add()
script += 'hold_key 342\nhold_key 340\nkey H\nrelease_key 340\nrelease_key 342\nwait_frames 200\nexpect_text "2 callers of add"\nscreenshot callers\n'
script += 'click_ui repo_search_close\nwait_frames 3\n' + caret_on_add()
script += 'hold_key 342\nkey F12\nrelease_key 342\nwait_frames 200\nexpect_text "inline int add(int a, int b)"\nscreenshot hover\n'
(out / 'journey.e2e').write_text(script)
with (out / 'run.log').open('w') as log:
    result = subprocess.run([str(args.binary), str(repo), '--test-mode', '--headless', f'--test-script={out / "journey.e2e"}',
                             f'--screenshot-dir={out}', '--e2e-timeout=180'], env=dict(os.environ, FH_NATIVE_MENUS='1'),
                            stdout=log, stderr=subprocess.STDOUT, timeout=240)
errors = [l for l in (out / 'run.log').read_text().splitlines() if 'E2E ERROR' in l or 'TIMEOUT' in l]
assert result.returncode == 0 and not errors, (errors[:5], out / 'run.log')
probe = json.loads((out / 'definition.reading.json').read_text())
print(f'PASS F12 opened math.h:120 in {probe["ready_ms"]:.0f} ms; references, callers and type shown through clangd', flush=True)

# Unavailable: a revision document is refused, and a language with no server
# installed says which server it needs.
(repo / 'lib.rs').write_text('fn main() {}\n')
script = ('resize 1400 900\nwait_for_refresh\nwait_frames 10\n'
          'hold_key 343\nkey P\nrelease_key 343\nwait_for_refresh\nclick_ui file_picker_input\nkey CMD+A\ntype "lib.rs"\n'
          'wait_frames 3\nwait_for_refresh\nkey ENTER\nwait_for_refresh\nwait_frames 10\nkey CTRL+G\nwait_frames 4\n'
          'click_ui line_picker_input\nkey CMD+A\ntype "1"\nkey ENTER\nwait_frames 10\nkey F12\nwait_frames 10\n')
script += ('expect_text "Rust navigation needs rust-analyzer"\nscreenshot missing_server\n' if not shutil.which('rust-analyzer') else '')
script += ('click_text "Semantic fixture"\nwait_for_refresh\nwait_frames 20\n'
           'right_click_ui jump_to_diff:a.cpp\nwait_frames 3\nclick_text "Open source"\nwait_for_refresh\nwait_frames 20\n'
           'key F12\nwait_frames 10\nexpect_text "Language servers read the working tree"\nscreenshot revision_refused\n')
(out / 'unavailable.e2e').write_text(script)
with (out / 'unavailable.log').open('w') as log:
    result = subprocess.run([str(args.binary), str(repo), '--test-mode', '--headless', f'--test-script={out / "unavailable.e2e"}',
                             f'--screenshot-dir={out}', '--e2e-timeout=120'], env=dict(os.environ, FH_NATIVE_MENUS='1'),
                            stdout=log, stderr=subprocess.STDOUT, timeout=180)
errors = [l for l in (out / 'unavailable.log').read_text().splitlines() if 'E2E ERROR' in l or 'TIMEOUT' in l]
assert result.returncode == 0 and not errors, (errors[:5], out / 'unavailable.log')
print('PASS revision document refused; missing server named', flush=True)

# Stale: a slow fake clangd answers after the file changed underneath it.
fake = out / 'fakebin'
fake.mkdir()
(fake / 'clangd').write_text(f"""#!/usr/bin/env python3
import json, sys, time
def read():
    length = 0
    while True:
        line = sys.stdin.buffer.readline()
        if not line: sys.exit(0)
        if line in (b'\\r\\n', b'\\n'): break
        if line.lower().startswith(b'content-length:'): length = int(line.split(b':')[1])
    return json.loads(sys.stdin.buffer.read(length))
def send(message):
    body = json.dumps(message).encode()
    sys.stdout.buffer.write(b'Content-Length: %d\\r\\n\\r\\n' % len(body) + body)
    sys.stdout.buffer.flush()
while True:
    m = read()
    if 'id' not in m: continue
    if m['method'] == 'initialize': send({{'jsonrpc': '2.0', 'id': m['id'], 'result': {{'capabilities': {{}}}}}})
    elif m['method'] == 'textDocument/definition':
        open('{out / 'asked'}', 'w').close()
        time.sleep(3)
        uri = m['params']['textDocument']['uri']
        send({{'jsonrpc': '2.0', 'id': m['id'], 'result': [{{'uri': uri, 'range': {{'start': {{'line': 0, 'character': 0}}, 'end': {{'line': 0, 'character': 1}}}}}}]}})
    else: send({{'jsonrpc': '2.0', 'id': m['id'], 'result': None}})
""")
(fake / 'clangd').chmod(0o755)
script = 'resize 1400 900\nwait_for_refresh\nwait_frames 10\n' + caret_on_add() + 'key F12\nwait_for_path changed\n'
script += 'wait_frames 30\nwait_for_path settled\nwait_frames 30\nexpect_text "changed while clangd answered"\nscreenshot stale\n'
(out / 'stale.e2e').write_text(script)
log = (out / 'stale.log').open('w')
app = subprocess.Popen([str(args.binary), str(repo), '--test-mode', '--headless', f'--test-script={out / "stale.e2e"}',
                        f'--screenshot-dir={out}', '--e2e-timeout=120'],
                       env=dict(os.environ, FH_NATIVE_MENUS='1', PATH=f'{fake}:{os.environ["PATH"]}'), stdout=log, stderr=subprocess.STDOUT)
import time
deadline = time.time() + 60
while not (out / 'asked').exists() and app.poll() is None and time.time() < deadline:
    time.sleep(0.05)
with (repo / 'a.cpp').open('a') as f:
    f.write('// edited while the server was answering\n')
(out / 'changed').write_text('')
time.sleep(4)  # the fake answers 3 s after it was asked
(out / 'settled').write_text('')
code = app.wait(timeout=120)
log.close()
errors = [l for l in (out / 'stale.log').read_text().splitlines() if 'E2E ERROR' in l or 'TIMEOUT' in l]
assert code == 0 and not errors, (errors[:5], out / 'stale.log')
print('PASS an answer about a file that changed meanwhile is dropped as stale', flush=True)
