import argparse
import json
import os
from pathlib import Path
import subprocess

# Working-tree comments follow their reviewed change into the commit: matched
# by saved content (the comment's line number is off by the unstaged lines
# above it), re-anchored to commit line numbers, with ambiguous matches and
# unstaged changes left on the working tree.
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
    settings = directory / 'settings'
    settings.mkdir()
    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Carry fixture'),
                    ('config', 'user.email', 'carry@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*command)
    for name in 'abd':
        (repo / f'{name}.cpp').write_text(''.join(f'int {name}_{i:03} = 0;\n' for i in range(1, 21)))
    git('add', '.')
    git('commit', '-qm', 'base')
    def edit(name, old, new):
        p = repo / f'{name}.cpp'
        p.write_text(p.read_text().replace(old, new))
    edit('a', 'int a_005 = 0;', 'int a_005 = 500;')
    edit('d', 'int d_006 = 0;', 'needle')
    edit('d', 'int d_007 = 0;', 'needle')
    git('add', '.')
    edit('a', 'int a_001 = 0;\n', 'top 1\ntop 2\nint a_001 = 0;\n')
    edit('b', 'int b_005 = 0;', 'int b_005 = 5;')
    env = dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings), FH_TEST_PERSIST_REVIEW='1')
    def replay(name, script):
        run = directory / name
        run.mkdir()
        setup = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        (run / 'journey.e2e').write_text(setup + script)
        command = [str(binary), str(repo), '--test-mode', f'--test-script={run / "journey.e2e"}', f'--screenshot-dir={run}', '--e2e-timeout=120', '--headless']
        with (run / 'run.log').open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        assert result.returncode == 0, run / 'run.log'
    replay('capture', 'click_ui review_unstaged_changes\nwait_for_refresh\nclick_ui save_review_baseline\nwait_frames 10\n')
    record = next((settings / 'reviews').glob('*.json'))
    data = json.loads(record.read_text())
    def comment(path, line, context):
        return dict(scope='wt', file=path, line=line, end_line=line, old_side=False, resolved=False,
                    text='Review ' + path, revision='', code_context=f'{line}: {context}\n', kind='Comment')
    data['comments'] = [comment('a.cpp', 7, 'int a_005 = 500;'), comment('b.cpp', 5, 'int b_005 = 5;'), comment('d.cpp', 9, 'needle')]
    record.write_text(json.dumps(data))
    replay('commit', 'click_button "Commit"\nwait_frames 5\nscreenshot dialog\nclick_button "Commit Staged Only"\nwait_frames 3\n'
           'screenshot committed\nexpect_text "Moved 1 comment to commit"\n'
           'expect_text "1 comment left on the working tree: matches several places in the commit"\nwait_frames 30\n')
    head = git('rev-parse', 'HEAD')
    assert git('log', '-1', '--format=%s') == 'Update'
    comments = {c['file']: c for c in json.loads(record.read_text())['comments']}
    a = comments['a.cpp']
    assert a['scope'] == head and a['line'] == 5 and a['end_line'] == 5, a
    assert a['revision'].startswith(head + '; hunk ') and '5: int a_005 = 500;' in a['code_context'], a
    assert comments['b.cpp']['scope'] == 'wt' and comments['d.cpp']['scope'] == 'wt', comments
    print(f'PASS {zoom}% committed comment moved by content to commit lines; unstaged and ambiguous stay', flush=True)
