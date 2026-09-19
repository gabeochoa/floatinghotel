import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
def git(*arguments):
    return subprocess.check_output(['git', '-C', str(repo), *arguments], text=True, stderr=subprocess.STDOUT).strip()
for arguments in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Split fixture'),
                  ('config', 'user.email', 'reader@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*arguments)
count = 3000
code = repo / 'large.cpp'
code.write_text(''.join(f'int value_{i} = 0;\n' for i in range(1, count + 1)))
git('add', '.')
git('commit', '-qm', 'Replacement baseline')
code.write_text(''.join(f'int value_{i} = 1;\n' for i in range(1, count + 1)))
git('commit', '-qam', 'Large replacement')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
results = []
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1800 1200\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Large replacement"\nwait_for_refresh\nwait_frames 15\nclick_ui diff_mode_inactive\nwait_frames 20\nscreenshot top\nbench_frames 120\nexpect_p99_below 20\n'
    script += 'hover_ui commit_detail_scroll\nscroll_wheel 0 -100000\nwait_frames 40\nscreenshot bottom\nbench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        run = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert run.returncode == 0, directory / 'run.log'
    hashes = None
    for name in ['top', 'bottom']:
        snapshot = json.loads((directory / f'{name}.json').read_text())
        before = {r['line'] for r in snapshot['reading_rows'] if r['path'] == 'large.cpp' and r['sign'] == '-'}
        after = {r['line'] for r in snapshot['reading_rows'] if r['path'] == 'large.cpp' and r['sign'] == '+'}
        assert before == after and 0 < len(before) < count // 4, (zoom, name, len(before), len(after))
        assert (1 in after if name == 'top' else count in after), (zoom, name, min(after), max(after))
        measured = snapshot['reading_performance']
        current_hashes = (measured['hunk_hash_scans'], measured['file_hash_scans'])
        if hashes is not None: assert current_hashes == hashes, (zoom, 'Repeated review hashing', hashes, current_hashes)
        hashes = current_hashes
        assert measured['prepared_pairs'] == 0, (zoom, name, measured)
        assert measured['intraline_comparisons'] == len(after), (zoom, name, measured)
        assert measured['metrics_bytes'] <= 5 * 1024 * 1024
        results.append({'measured': measured, 'zoom': zoom, 'position': name, 'emitted_paired_lines': len(after),
                        'legacy_eager_comparisons_per_frame': count,
                        'comparison_calls': measured['intraline_comparisons']})
    print(f'PASS {zoom}% 3,000-pair split diff: bounded emitted pairs, endpoints and frame gate', flush=True)
assert git('status', '--porcelain') == ''
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps({'passed': True, 'binary_sha256': digest, 'results': results,
    'method': 'Frame counters record preparation, emitted pairs, and intraline comparisons; rendered rows independently verify endpoints and counts.'}, indent=2) + '\n')
