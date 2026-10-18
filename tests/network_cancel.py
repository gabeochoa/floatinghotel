import argparse
import os
from pathlib import Path
import subprocess

# Network Git operations against a disposable local remote that stalls: the
# status bar shows the operation, Cancel kills it (no stray process left), the
# remote ref does not move, and nothing is killed by a fixed timeout first.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo, remote = out / 'fixture', out / 'remote.git'

def git(*args, cwd=None):
    return subprocess.check_output(['git', '-C', str(cwd or repo), *args], text=True).strip()

subprocess.check_call(['git', 'init', '--bare', '-q', '-b', 'main', str(remote)])
repo.mkdir()
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Network fixture'),
                ('config', 'user.email', 'network@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
(repo / 'a.txt').write_text('base\n')
git('add', '.')
git('commit', '-qm', 'base')
git('remote', 'add', 'origin', str(remote))
git('push', '-q', '-u', 'origin', 'main')
pushed = git('rev-parse', 'main', cwd=remote)
(repo / 'a.txt').write_text('local\n')
git('commit', '-qam', 'local')

# Both transports stall far past the old 60 s bound unless cancelled.
for name, program in [('upload', 'git-upload-pack'), ('receive', 'git-receive-pack')]:
    wrapper = out / f'slow-{name}-pack'
    wrapper.write_text(f'#!/bin/sh\nsleep 600\nexec {program} "$@"\n')
    wrapper.chmod(0o755)
    git('config', f'remote.origin.{name}pack', str(wrapper))

def journey(label, start, activity, toast):
    directory = out / label
    directory.mkdir()
    script = 'resize 1280 720\nwait_for_refresh\nwait_frames 10\n' + start
    script += f'expect_text "{activity}"\nscreenshot running\nclick_ui cancel_active_read\n'
    script += f'expect_text "{toast}"\nwait_frames 10\nscreenshot cancelled\n'
    (directory / 'journey.e2e').write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(ROOT / 'output/floatinghotel.exe'), str(repo), '--test-mode', '--headless', f'--test-script={directory / "journey.e2e"}',
            f'--screenshot-dir={directory}', '--e2e-timeout=90'], cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'),
            stdout=log, stderr=subprocess.STDOUT, timeout=150)
    assert result.returncode == 0, directory / 'run.log'
    stray = subprocess.run(['pgrep', '-f', str(out / 'slow-')], capture_output=True, text=True).stdout.split()
    assert not stray, (label, 'stalled transport still running', stray)
    assert git('rev-parse', 'main', cwd=remote) == pushed, (label, 'remote ref moved')
    print(f'PASS {label}: "{activity}" shown, Cancel stopped it, no stray process, remote unchanged', flush=True)

journey('fetch', 'native_menu_action "Fetch"\n', 'Fetch...', 'Fetch cancelled')
journey('pull', 'native_menu_action "Pull"\n', 'Pull...', 'Pull cancelled')
# The toolbar (sidebar hidden) pushes straight to upstream; the Push dialog
# reads its destination asynchronously, which a headless script cannot wait on.
journey('push', 'native_menu_action "Toggle Sidebar"\nwait_frames 5\nclick_text "Push"\n', 'Push...', 'Push cancelled')
