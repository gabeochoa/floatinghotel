import argparse
import os
from pathlib import Path
import subprocess

# The commit box starts from the repository's commit.template (comment lines
# dropped), refuses to commit it untouched, and its right-click menu puts a
# conventional-commit type on the subject without touching the rest.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Template fixture'),
                ('config', 'user.email', 'template@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
(repo / 'a.txt').write_text('base\n')
(repo / '.gitmessage').write_text('# Say why, not what\nadd summary\n\nRefs: TICKET\n')
git('add', '.')
git('commit', '-qm', 'base')
git('config', 'commit.template', '.gitmessage')
(repo / 'a.txt').write_text('changed\n')
git('add', 'a.txt')

# Only the subject line is asserted on screen: the box is one line tall and
# labels clip to their own box (afterhours 6327e72), so the body line is
# scrolled out of view. The committed message below checks the body.
script = '''resize 1280 720
wait_for_refresh
wait_frames 10
expect_text "add summary"
expect_no_text "Say why"
screenshot prefilled
click_ui commit_msg_input
key ENTER
wait_frames 10
expect_text "Fill in the commit template first"
right_click_ui commit_msg_input
wait_frames 5
click_text "fix:"
wait_frames 5
expect_text "fix: add summary"
screenshot typed
key ENTER
wait_for_refresh
wait_frames 10
expect_text "add summary"
screenshot refilled
'''
(out / 'journey.e2e').write_text(script)
with (out / 'run.log').open('w') as log:
    result = subprocess.run([str(ROOT / 'output/floatinghotel.exe'), str(repo), '--test-mode', '--headless', f'--test-script={out / "journey.e2e"}',
        f'--screenshot-dir={out}', '--e2e-timeout=60'], cwd=ROOT, env=dict(os.environ), stdout=log, stderr=subprocess.STDOUT, timeout=120)
assert result.returncode == 0, out / 'run.log'
message = git('log', '-1', '--format=%B')
assert message == 'fix: add summary\n\nRefs: TICKET', repr(message)
assert git('rev-list', '--count', 'HEAD') == '2'
print('PASS template prefilled without comments, untouched template refused, "fix:" prefixed, '
      'subject/body kept, box refilled after commit', flush=True)
