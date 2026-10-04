import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

# The file header's context menu copies that file's diff or path; Edit › Copy
# Diff exports every file in the review and Edit › Copy Path the reading file.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
guard = ROOT / 'output/clipboard_guard'
marker = os.environ.get('FH_TEST_CLIPBOARD_MARKER')
if not marker:
    subprocess.run(['swiftc', str(ROOT / 'tests/clipboard_guard.swift'), '-o', str(guard)], check=True)
    marker = 'FHCOPY_' + uuid.uuid4().hex
    raise SystemExit(subprocess.run([str(guard), 'guard', marker, sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]).returncode)
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
binary = ROOT / 'output/floatinghotel.exe'
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True)

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Copy fixture'),
                ('config', 'user.email', 'copy@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
for name in ['a.txt', 'b.txt']:
    (repo / name).write_text(''.join(f'{name} {i}\n' for i in range(1, 21)))
git('add', '.')
git('commit', '-qm', 'base')
for name in ['a.txt', 'b.txt']:
    (repo / name).write_text((repo / name).read_text().replace(f'{name} 5\n', f'{name} five {marker}\n'))

def file_diff(name):
    body = git('diff', '--', name)
    return f'--- a/{name}\n+++ b/{name}\n' + body[body.index('@@'):]

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    setup = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    setup += 'wait_for_refresh\nscreenshot zoom_ready\nclick_text "Unstaged (2)"\nwait_for_refresh\nwait_frames 10\n'
    def copied(label, script):
        path = directory / f'{label}.e2e'
        path.write_text(setup + script)
        with (directory / f'{label}.log').open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=120'],
                cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_NATIVE_HIDDEN='1'), stdout=log, stderr=subprocess.STDOUT, timeout=180)
        assert result.returncode == 0, (zoom, label, directory / f'{label}.log')
        evidence = directory / f'{label}.clipboard.json'
        subprocess.run([str(guard), 'read', marker, str(evidence)], check=True)
        return json.loads(evidence.read_text())['text']
    header = 'right_click_ui file_header_row\nwait_frames 3\nscreenshot {0}\nclick_ui "context_menu_item_{1}"\nwait_frames 3\n'
    assert copied('header_diff', header.format('header_menu', 'Copy diff')) == file_diff('a.txt'), zoom
    assert copied('header_path', header.format('header_path', 'Copy path')) == 'a.txt', zoom
    assert copied('menu_diff', 'native_menu_action "Copy Diff"\nwait_frames 3\nexpect_text "Copied diff of 2 files"\n') == file_diff('a.txt') + file_diff('b.txt'), zoom
    assert copied('menu_path', 'click_ui hunk_header_label\nkey J\nwait_frames 10\nnative_menu_action "Copy Path"\nwait_frames 3\nexpect_text "Copied b.txt"\n') == 'b.txt', zoom
    print(f'PASS {zoom}% header menu copies diff/path; Edit › Copy Diff exports the review, Copy Path the reading file', flush=True)
