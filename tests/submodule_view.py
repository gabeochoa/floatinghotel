import argparse
import os
from pathlib import Path
import subprocess

# A gitlink change shows its commit range from the submodule checkout and opens
# the submodule as a repository tab; a pointer to a commit the checkout lacks
# and an uninitialized submodule each say so instead of showing a range.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
binary = ROOT / 'output/floatinghotel.exe'

def git(repo, *args):
    return subprocess.check_output(['git', '-c', 'protocol.file.allow=always', '-C', str(repo), *args], text=True).strip()

def init(repo):
    repo.mkdir(parents=True)
    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Submodule fixture'),
                    ('config', 'user.email', 'sub@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(repo, *command)

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    upstream = directory / 'upstream'
    init(upstream)
    (upstream / 'lib.txt').write_text('v1\n')
    git(upstream, 'add', '.')
    git(upstream, 'commit', '-qm', 'Library v1')
    repo = directory / 'super'
    init(repo)
    git(repo, 'submodule', 'add', '-q', str(upstream), 'sub')
    git(repo, 'commit', '-qm', 'Add submodule')
    checkout = repo / 'sub'
    git(checkout, 'config', 'user.name', 'Submodule fixture')
    git(checkout, 'config', 'user.email', 'sub@example.invalid')
    for n in [2, 3]:
        (checkout / 'lib.txt').write_text(f'v{n}\n')
        git(checkout, 'commit', '-qam', f'Library v{n}')
    def replay(name, script):
        run = directory / name
        run.mkdir()
        setup = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        (run / 'journey.e2e').write_text(setup + script)
        command = [str(binary), str(repo), '--test-mode', f'--test-script={run / "journey.e2e"}', f'--screenshot-dir={run}', '--e2e-timeout=120', '--headless']
        with (run / 'run.log').open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=180)
        assert result.returncode == 0, run / 'run.log'
    replay('range', 'click_ui review_unstaged_changes\nwait_for_refresh\nwait_frames 10\nwait_for_refresh\nwait_frames 5\nscreenshot range\n'
           'expect_text "Fast-forward · 2 commits added"\nexpect_text "Library v3"\nexpect_text "Library v2"\n'
           'click_ui open_submodule\nwait_for_refresh\nwait_frames 10\nscreenshot opened\nexpect_text "sub (main)"\n')
    # Stage the forward pointer, then point the working tree at a commit the checkout lacks.
    git(repo, 'add', 'sub')
    git(repo, 'commit', '-qm', 'Bump library')
    git(repo, 'update-index', '--cacheinfo', '160000,' + 'f' * 40 + ',sub')
    replay('missing_object',            'click_ui review_staged_changes\nwait_for_refresh\nwait_frames 10\nwait_for_refresh\nwait_frames 5\nscreenshot missing_object\n'
           'expect_text "Commit ffffffffffff is not in the submodule checkout"\n')
    git(repo, 'reset', '-q')
    git(repo, 'submodule', '--quiet', 'deinit', '-f', 'sub')
    replay('uninitialized',            'click_text "Bump library"\nwait_for_refresh\nkey ENTER\nwait_for_refresh\nwait_frames 10\nwait_for_refresh\nwait_frames 5\nscreenshot uninitialized\n'
           'expect_text "Submodule is not initialized"\n')
    print(f'PASS {zoom}% range + open, missing object, uninitialized checkout', flush=True)
