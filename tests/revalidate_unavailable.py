import argparse
import os
from pathlib import Path
import subprocess
import time

# A warm switch shows a commit's patch straight from the cache, then validates
# it. If the commit object is gone by then, the error must replace the cached
# patch rather than leave stale content on screen.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Revalidate fixture'),
                ('config', 'user.email', 'revalidate@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('config', 'gc.auto', '0')]:
    git(*command)
for i, subject in enumerate(['Root commit', 'Doomed commit', 'Tip commit']):
    (repo / 'a.txt').write_text(f'{i}\n')
    git('add', '.')
    git('commit', '-qm', subject)
doomed = git('rev-parse', 'HEAD~1')

script = '''resize 1280 720
wait_for_refresh
wait_frames 10
click_text "Doomed commit"
wait_for_refresh
wait_frames 30
expect_text "Doomed commit"
click_text "Tip commit"
wait_for_refresh
wait_frames 30
screenshot away
wait_for_path deleted
click_text "Doomed commit"
wait_frames 60
expect_text "Unable to resolve commit"
screenshot unavailable
'''
(out / 'journey.e2e').write_text(script)
with (out / 'run.log').open('w') as log:
    app = subprocess.Popen([str(ROOT / 'output/floatinghotel.exe'), str(repo), '--test-mode', '--headless', f'--test-script={out / "journey.e2e"}',
        f'--screenshot-dir={out}', '--e2e-timeout=60'], cwd=ROOT, env=dict(os.environ), stdout=log, stderr=subprocess.STDOUT)
    deadline = time.time() + 60
    while not (out / 'away.png').exists() and app.poll() is None and time.time() < deadline:
        time.sleep(0.05)
    (repo / '.git/objects' / doomed[:2] / doomed[2:]).unlink()
    (out / 'deleted').write_text('')
    assert app.wait(timeout=90) == 0, out / 'run.log'
print('PASS cached patch shown on return was replaced by the unavailable-commit error', flush=True)
