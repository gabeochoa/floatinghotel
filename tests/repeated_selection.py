import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from reading_journey import fixture

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--zooms', nargs='+', type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
head = fixture(repo)['head']
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
def capture(name, count):
    return f'wait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
results = []
for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'
    settings.mkdir()
    script = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'screenshot ready\nclick_text "Reading change"\nwait_for_refresh\nscreenshot commit\nclick_ui jump_to_diff:alpha.cpp\n' + capture('review_before', 2)
    for label, action in [('row', 'click_ui jump_to_diff:alpha.cpp'), ('history', 'click_text "Reading change"'), ('tab', 'click_ui content_document_2')]:
        script += f'reading_probe {label} review alpha.cpp {head}\n{action}\nreading_checkpoint\n' + capture(label, 2)
    script += 'key CMD+P\nwait_for_refresh\nscreenshot picker\nclick_ui file_picker_input\nkey CMD+A\ntype alpha.cpp\nkey ENTER\nwait_for_refresh\nscreenshot source\nfocus_ui diff_scroll\nkey RIGHT\nkey SHIFT+RIGHT\n' + capture('source_before', 3)
    script += f'reading_probe source_tab source alpha.cpp {head}\nclick_ui content_document_3\nreading_checkpoint\n' + capture('source_tab', 3)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=90'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_NATIVE_HIDDEN='1', FH_TEST_SETTINGS_DIR=str(settings)),
            stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, directory / 'run.log'
    def workspace(label): return json.loads((directory / f'{label}.workspace.json').read_text())
    for label in ['row', 'history', 'tab', 'source_tab']:
        before = workspace('source_before' if label == 'source_tab' else 'review_before')
        after = workspace(label)
        assert after['active'] == before['active'] and after['history'] == before['history'], (zoom, label, before, after)
        first = next(t for t in before['tabs'] if t['id'] == before['active'])
        last = next(t for t in after['tabs'] if t['id'] == after['active'])
        for key in ['selection', 'caret']:
            assert first.get(key) == last.get(key), (zoom, label, key)
        if label == 'source_tab': assert first.get('selection')
        probe = json.loads((directory / f'{label}.reading.json').read_text())
        for cache in ['blob_cache', 'patch_cache']:
            assert probe[cache]['hit_delta'] == probe[cache]['miss_delta'] == 0, (zoom, label, probe)
        results.append(dict(zoom=zoom, action=label, blob=probe['blob_cache'], patch=probe['patch_cache']))
    assert 'Native test window hidden=1 key=0' in (directory / 'run.log').read_text()
    print(f'PASS {zoom}% repeated row, history, review tab and source tab: no reads or lost selection', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, results=results), indent=2) + '\n')
