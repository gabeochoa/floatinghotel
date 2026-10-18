"""Native session check: real menu tracking, shortcut delivery and a live
resize, driven through the window server in a normal (visible) session.

Hidden native probes and headless screenshots cannot show these; this check
posts the same events a person produces and reads the app's own state back.
It needs Accessibility permission for the terminal (System Settings ->
Privacy & Security -> Accessibility) and takes over the mouse for a few
seconds, so run it at the machine:

    python3 tests/native_session/check.py --output output/native-session-NEW
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
helper = out / 'post_events'
subprocess.run(['swiftc', '-O', str(HERE / 'post_events.swift'), '-o', str(helper)], check=True)
if subprocess.run([str(helper), 'trusted'], capture_output=True, text=True).stdout.strip() != 'yes':
    print('NOT RUN: grant Accessibility permission to this terminal, then rerun')
    sys.exit(2)

settings = out / 'settings'
settings.mkdir()
# Each gate is a file the harness creates after posting events; the script
# then records what the app saw.
script = '\n'.join([
    'resize 1200 800', 'wait_for_refresh', 'wait_frames 30', 'screenshot ready', 'wait_for_path menu_done', 'wait_frames 30',
    'screenshot after_menu', 'wait_for_path chord_done', 'wait_frames 30', 'screenshot after_chord', 'key ESCAPE',
    'wait_for_path drag_done', 'wait_frames 30', 'screenshot after_drag', '']) + '\n'
(out / 'session.e2e').write_text(script)
log = (out / 'run.log').open('w')
app = subprocess.Popen([str(args.binary), str(ROOT / 'tests/fixture_repo'), '--test-mode', f'--test-script={out / "session.e2e"}',
                        f'--screenshot-dir={out}', '--e2e-timeout=120'],
                       env=dict(os.environ, FH_NATIVE_MENUS='1', FH_RESIZE_TIMING='1', FH_TEST_SETTINGS_DIR=str(settings)),
                       stdout=log, stderr=subprocess.STDOUT)


def wait_shot(name, timeout=60):
    deadline = time.time() + timeout
    while not (out / f'{name}.png').exists():
        if app.poll() is not None or time.time() > deadline:
            raise SystemExit(f'app stopped before {name}; see {out / "run.log"}')
        time.sleep(0.1)


def post(*command):
    result = subprocess.run([str(helper), *command], capture_output=True, text=True)
    (out / 'events.log').open('a').write(' '.join(command) + ' -> ' + (result.stdout + result.stderr).strip() + '\n')
    return result


def shot(name):
    return json.loads((out / f'{name}.json').read_text())


results = {}
wait_shot('ready')
before = shot('ready')['ui_scale']
menu = post('menu', str(app.pid), 'View', 'Zoom In')
(out / 'menu_done').write_text('')
wait_shot('after_menu')
after = shot('after_menu')['ui_scale']
results['menu_tracking'] = {'pass': menu.returncode == 0 and after > before,
                            'detail': f'View > Zoom In through the real menu bar: ui_scale {before} -> {after}'}

post('chord', str(app.pid), '35', 'cmd')  # kVK_ANSI_P
(out / 'chord_done').write_text('')
wait_shot('after_chord')
picker = any(n.get('name') == 'file_picker_input' and n.get('rendered') for n in shot('after_chord')['nodes'])
results['shortcut_delivery'] = {'pass': picker, 'detail': 'Cmd+P posted as a key event opened Quick Open'}

drag = post('drag', str(app.pid), '240', '160', '40')
(out / 'drag_done').write_text('')
wait_shot('after_drag')
viewport_before, viewport_after = shot('after_chord')['viewport'], shot('after_drag')['viewport']
app.wait(timeout=120)
log.close()
frames = [float(m) for m in re.findall(r'RESIZE frame=\d+x\d+ elapsed_ms=([\d.]+)', (out / 'run.log').read_text())]
results['live_resize'] = {
    'pass': drag.returncode == 0 and viewport_after['width'] > viewport_before['width'] and len(frames) >= 10,
    'detail': f"{drag.stdout.strip()}; viewport {viewport_before['width']}x{viewport_before['height']} -> "
              f"{viewport_after['width']}x{viewport_after['height']}; {len(frames)} frames drawn during the drag"
              + (f", draw max {max(frames):.2f} ms, p50 {sorted(frames)[len(frames) // 2]:.2f} ms" if frames else '')}
(out / 'result.json').write_text(json.dumps(results, indent=1))
for name, r in results.items():
    print(('PASS ' if r['pass'] else 'FAIL ') + name + ': ' + r['detail'])
sys.exit(0 if all(r['pass'] for r in results.values()) else 1)
