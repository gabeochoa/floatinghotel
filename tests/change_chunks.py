import argparse
import os
from pathlib import Path
import subprocess

# A hunk holding two adjacent 12-line replacements offers each as its own
# "Approve lines" part. Approving the second leaves the first unstaged, and the
# remaining hunk (now one part) stages whole.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
binary = ROOT / 'output/floatinghotel.exe'

for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    repo = directory / 'fixture'
    repo.mkdir(parents=True)
    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args], text=True)
    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Chunk fixture'),
                    ('config', 'user.email', 'chunk@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*command)
    source = repo / 'a.txt'
    source.write_text(''.join(f'line {i}\n' for i in range(1, 61)))
    git('add', '.')
    git('commit', '-qm', 'base')
    edited = ''.join((f'changed {i}' if 10 <= i <= 15 or 17 <= i <= 22 else f'line {i}') + '\n' for i in range(1, 61))
    source.write_text(edited)
    def replay(name, script):
        run = directory / name
        run.mkdir()
        setup = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        (run / 'journey.e2e').write_text(setup + 'click_text "Unstaged (1)"\nwait_for_refresh\nclick_text "a.txt"\nwait_for_refresh\nwait_frames 10\n' + script)
        command = [str(binary), str(repo), '--test-mode', f'--test-script={run / "journey.e2e"}', f'--screenshot-dir={run}', '--e2e-timeout=120', '--headless']
        with (run / 'run.log').open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=180)
        assert result.returncode == 0, run / 'run.log'
    replay('part', 'right_click_ui hunk_header_label\nwait_frames 3\nscreenshot parts\n'
           'expect_text "Approve lines 10–15 · +6 −6"\n'
           'click_ui "context_menu_item_Approve lines 17–22 · +6 −6"\nwait_for_refresh\nwait_frames 10\nscreenshot part_staged\n')
    cached = git('diff', '--cached')
    assert '+changed 17' in cached and '+changed 22' in cached and 'changed 15' not in cached, cached
    assert '+changed 10' in git('diff'), (zoom, 'first part should stay unstaged')
    replay('rest', 'right_click_ui hunk_header_label\nwait_frames 3\nexpect_no_text "Approve lines"\n'
           'click_ui "context_menu_item_Approve hunk"\nwait_for_refresh\nwait_frames 10\n')
    assert git('diff') == '' and git('show', ':a.txt') == edited, zoom
    print(f'PASS {zoom}% parts listed, second part staged alone, remainder staged whole', flush=True)
