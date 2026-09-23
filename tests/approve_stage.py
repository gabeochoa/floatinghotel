#!/usr/bin/env python3
"""Approve means stage (git add -p): hunk approve stages async, fully
staged files leave the unstaged section, staged+unstaged share one page."""
import json, os, subprocess, sys
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]

with TemporaryDirectory(prefix='fh-approve-stage-') as temp:
    root = Path(temp)
    repo, out = root / 'repo', root / 'run'
    repo.mkdir(); out.mkdir()
    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Approve fixture'),
                    ('config', 'user.email', 'approve@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*command)
    lines = [f'line {i}' for i in range(1, 101)]
    (repo / 'a.cpp').write_text('\n'.join(lines) + '\n')
    (repo / 'b.cpp').write_text('\n'.join(lines) + '\n')
    git('add', '.'); git('commit', '-qm', 'base')
    changed = lines.copy(); changed[9] = 'ten approved'; changed[69] = 'seventy left'
    (repo / 'a.cpp').write_text('\n'.join(changed) + '\n')
    changed_b = lines.copy(); changed_b[9] = 'b whole file'
    (repo / 'b.cpp').write_text('\n'.join(changed_b) + '\n')

    settle = 'wait_frames 3\nwait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 20\n'
    script = '\n'.join(['resize 1800 1200', 'wait_for_refresh',
        'click_ui review_unstaged_changes', 'wait_for_refresh',
        'screenshot before',
        'click_ui approve_hunk_btn', settle, 'screenshot after_hunk',
        'click_ui jump_to_diff:b.cpp', 'wait_frames 10',
        'click_ui stage_file_btn', settle, 'screenshot after_file',
        'expect_text "Staged"', 'expect_text "Unstaged"'])
    script_path = out / 'journey.e2e'
    script_path.write_text(script + '\n')
    with (out / 'run.log').open('w') as log:
        result = subprocess.run([str(ROOT / 'output/floatinghotel.exe'), str(repo), '--test-mode', '--headless',
            f'--test-script={script_path}', f'--screenshot-dir={out}', '--e2e-timeout=120'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=150)
    assert result.returncode == 0, (out / 'run.log').read_text(errors='replace')
    def diff(*args): return subprocess.check_output(['git', '-C', str(repo), 'diff', *args], text=True)
    staged, unstaged = diff('--cached'), diff()
    assert 'ten approved' in staged and 'seventy left' not in staged, staged
    assert 'seventy left' in unstaged and 'ten approved' not in unstaged, unstaged
    assert 'b whole file' in staged and 'b whole file' not in unstaged, (staged, unstaged)
    print('PASS: approve stages hunk/file, staged+unstaged on one page')
