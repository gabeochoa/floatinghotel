import argparse
import os
from pathlib import Path
import shutil
import subprocess

# The ACP sidebar runs a real agent process and lets it edit through the
# editor. Scenario 1 drives the scripted fake agent (tests/fake_acp_agent.py)
# through initialize -> prompt -> fs/read -> permission banner -> fs/write
# and checks the file the "agent" wrote. Scenario 2 runs Muse's ACP
# server with the deterministic echo provider when `muse` is installed.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
binary = ROOT / 'output/floatinghotel.exe'
repo = out / 'fixture'
repo.mkdir()

def git(*a):
    return subprocess.check_output(['git', '-C', str(repo), *a], text=True)

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'ACP fixture'),
                ('config', 'user.email', 'acp@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
(repo / 'input.txt').write_text('hello sidebar agent\n')
(repo / 'shot.png').write_bytes(b'\x89PNG\r\n\x1a\nfake-image-bytes')
git('add', '.')
git('commit', '-qm', 'base')

def run(label, script, extra_env):
    path = out / f'{label}.e2e'
    path.write_text(script)
    env = dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_NATIVE_HIDDEN='1', **extra_env)
    with (out / f'{label}.log').open('w') as log:
        result = subprocess.run(
            [str(binary), str(repo), '--test-mode', f'--test-script={path}',
             f'--screenshot-dir={out}', '--e2e-timeout=120'],
            cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=240)
    assert result.returncode == 0, (label, out / f'{label}.log')

SETUP = 'resize 1600 1000\nwait_for_refresh\nwait_frames 5\n'

fake = f'python3 {ROOT / "tests/fake_acp_agent.py"}'
run('fake_agent', SETUP +
    'acp_start muse\n'
    'validate sidebar_mode=Agent\n'
    'acp_prompt uppercase it please\n'
    'wait_until 30 expect_text "Hello world"\n'
    'wait_until 30 expect_text "Allow: Uppercase the file"\n'
    'screenshot acp_permission\n'
    'acp_answer allow\n'
    'wait_until 30 expect_text "Uppercase the file · completed"\n'
    'wait_until 30 expect_text "TERMOK"\n'
    'wait_until 30 expect_text "Ready"\n'
    'validate acp_state=Ready\n'
    'screenshot acp_fake_done\n'
    'wait_until 30 expect_text "Model: Fake Mini"\n'
    'click_ui agent_model\n'
    'wait_until 30 expect_text "Model: Fake Max"\n'
    'click_ui agent_attach_image\n'
    'wait_until 30 expect_text "shot.png"\n'
    'acp_prompt look at this\n'
    'wait_until 30 expect_text "IMGOK image/png"\n'
    'wait_until 30 expect_text "Allow: Uppercase the file"\n'
    'acp_answer allow_once\n'
    'wait_frames 30\n'
    'click_ui agent_resume\n'
    'wait_until 30 expect_text "Old chat"\n'
    'click_ui agent_session_0\n'
    'wait_until 30 expect_text "earlier answer"\n'
    'screenshot acp_fake_resumed\n'
    'acp_stop\n',
    {'FH_ACP_MUSE_CMD': fake, 'FH_TEST_ATTACH_IMAGE': str(repo / 'shot.png')})
assert (repo / 'written.txt').read_text() == 'HELLO SIDEBAR AGENT\n', \
    "agent's fs/write_text_file did not land in the repo"
print('PASS fake agent: fs read/write + permission through the sidebar', flush=True)

if shutil.which('muse'):
    run('muse_echo', SETUP +
        'acp_start muse\n'
        'acp_prompt hello floating hotel\n'
        'wait_until 60 expect_text "echo: hello floating hotel"\n'
        'wait_until 60 expect_text "Ready"\n'
        'validate acp_state=Ready\n'
        'screenshot acp_muse_echo\n'
        'acp_stop\n',
        {'FH_ACP_MUSE_CMD': 'muse acp --provider echo'})
    print('PASS muse: live ACP session with the echo provider', flush=True)
else:
    print('SKIP muse echo (muse not on PATH)', flush=True)
