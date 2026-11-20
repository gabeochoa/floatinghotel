#!/usr/bin/env python3
"""Generated, binary and test files start folded in the aggregate
Unstaged changes document (the fold-noise setting, default on). A manual
unfold wins afterwards, and an ordinary file is never folded by default.

Deterministic counterpart to the flow script's fold section: asserts on
the screenshot JSON (fold controls present, reading rows per file)
instead of pixel/text timing.
Usage: tests/fold_noise_files.py [--output DIR]
"""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'output/floatinghotel.exe'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='/tmp/fh-fold-noise')
    args = parser.parse_args()
    out = Path(args.output)
    shutil.rmtree(out, ignore_errors=True)
    shots = out / 'shots'
    shots.mkdir(parents=True)
    repo = out / 'repo'
    repo.mkdir()

    def git(*argv):
        return subprocess.check_output(['git', '-C', str(repo), *argv], text=True).strip()

    for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Noise fixture'),
                    ('config', 'user.email', 'noise@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
        git(*command)
    lines = [f'int value_{i} = {i};' for i in range(1, 61)]
    (repo / 'main.cpp').write_text('\n'.join(lines) + '\n')
    (repo / 'parser_test.cpp').write_text('\n'.join(lines) + '\n')
    git('add', '.')
    git('commit', '-qm', 'base')
    for name in ['main.cpp', 'parser_test.cpp']:
        path = repo / name
        path.write_text(path.read_text().replace('value_10 = 10;', 'value_10 = 100;'))
    # A real PNG (binary) as an untracked generated_images artifact.
    import base64
    (repo / 'generated_images').mkdir()
    (repo / 'generated_images' / 'call_icon.png').write_bytes(base64.b64decode(
        'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=='))

    script = '\n'.join([
        'resize 1800 1200', 'wait_for_refresh',
        'click_ui review_unstaged_changes', 'wait_for_refresh', 'wait_frames 15',
        'click_ui working_review_toggle', 'wait_frames 15', 'wait_for_refresh',
        'wait_until 120 expect_text "generated_images/call_icon.png"', 'wait_frames 30',
        'screenshot aggregate',
        'mouse_move 900 600', 'scroll_wheel 0 100000', 'wait_frames 15',
        'click_ui fold_file:generated_images/call_icon.png', 'wait_frames 30',
        'wait_for_refresh', 'wait_frames 30',
        'screenshot unfolded',
        'click_ui fold_file:generated_images/call_icon.png', 'wait_frames 15',
        'screenshot refolded',
    ]) + '\n'
    (out / 'journey.e2e').write_text(script)
    # Warm-up launch: on a cold first launch every git spawn pays the
    # host's process-scan cost and the untracked review lands after the
    # journey's simulated waits have burned down. One throwaway open
    # settles the repo; the asserting journey then runs warm.
    (out / 'warm.e2e').write_text('resize 1800 1200\nwait_for_refresh\nwait_frames 120\nscreenshot warm\n')
    with (out / 'warm.log').open('w') as log:
        subprocess.run(
            [str(EXE), str(repo), '--test-mode', '--headless', f'--test-script={out / "warm.e2e"}',
             f'--screenshot-dir={shots}', '--e2e-timeout=120'],
            cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=150)
    before = EXE.stat().st_mtime_ns
    with (out / 'run.log').open('w') as log:
        result = subprocess.run(
            [str(EXE), str(repo), '--test-mode', '--headless', f'--test-script={out / "journey.e2e"}',
             f'--screenshot-dir={shots}', '--e2e-timeout=120'],
            cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=150)
    assert result.returncode == 0, out / 'run.log'
    assert EXE.stat().st_mtime_ns == before, 'binary changed during the run'

    def state(name):
        data = json.loads((shots / f'{name}.json').read_text())
        folds = {n['name'] for n in data['nodes'] if str(n.get('name', '')).startswith('fold_file:')}
        rows = {}
        for row in data.get('reading_rows', []):
            rows[row['path']] = rows.get(row['path'], 0) + 1
        # Card height per file: a folded card is just its 44px header; an
        # expanded binary card grows by its image body. Reading rows do
        # not cover binary bodies, so height is the honest signal there.
        labels = [(n['rect']['y'], n['text']) for n in data['nodes']
                  if n.get('name') == 'file_header_label' and n.get('rendered')]
        borders = [(n['rect']['y'], n['rect']['height']) for n in data['nodes']
                   if n.get('name') == 'file_card_border' and n.get('rendered')]
        heights = {}
        for y, label in labels:
            match = [h for by, h in borders if abs(by - (y - 6)) < 4]
            if match: heights[label] = match[0]
        return folds, rows, heights

    folds, rows, heights = state('aggregate')
    assert 'fold_file:parser_test.cpp' in folds and 'fold_file:main.cpp' in folds, folds
    # Test file and binary artifact start folded; the ordinary file does not.
    assert rows.get('parser_test.cpp', 0) == 0 and heights.get('generated_images/call_icon.png', 99) <= 44.1, (rows, heights)
    assert rows.get('main.cpp', 0) > 0, rows
    _, _, heights = state('unfolded')
    assert heights.get('generated_images/call_icon.png', 0) > 100, f'unfolding the binary card grows it: {heights}'
    _, _, heights = state('refolded')
    assert heights.get('generated_images/call_icon.png', 99) <= 44.1, f'folding it again collapses it: {heights}'
    print('PASS generated/binary/test files start folded in Unstaged; manual fold toggles win')


if __name__ == '__main__':
    main()
