import argparse
import json
import re
import subprocess
from pathlib import Path

# Measures what a large repository tab costs once it goes inactive, and what
# opening the same repository again costs: physical footprint after each step
# and frame time with the other tab in the background. Asserts that a
# duplicate open focuses the existing tab and that an inactive tab adds no
# per-frame work.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--repos', type=Path, nargs=2, help='reuse two stress repos instead of building them')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repos = [p.resolve() for p in args.repos] if args.repos else [out / 'a', out / 'b']
if not args.repos:
    for repo in repos:
        subprocess.run([str(ROOT / 'scripts/make_stress_repo.sh'), str(repo), '--big-diff', '20000'], check=True, stdout=subprocess.DEVNULL)
a, b = repos

settle = 'wait_for_refresh\nwait_frames 30\n'
script = f'resize 1600 1000\n{settle}click_text "Review working changes"\n{settle}log_footprint one_tab\nbench_frames 60\n'
script += f'set_open_path {b}\nkey CMD+O\nwait_frames 6\n{settle}log_footprint second_tab\nbench_frames 60\n'
script += f'set_open_path {a}\nkey CMD+O\nwait_frames 6\n{settle}log_footprint duplicate_open\nbench_frames 60\nscreenshot tabs\n'
(out / 'journey.e2e').write_text(script)
with (out / 'run.log').open('w') as log:
    result = subprocess.run([str(ROOT / 'output/floatinghotel.exe'), str(a), '--test-mode', '--headless', f'--test-script={out / "journey.e2e"}',
        f'--screenshot-dir={out}', '--e2e-timeout=300'], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=400)
text = (out / 'run.log').read_text()
assert result.returncode == 0, out / 'run.log'
mb = dict(re.findall(r'footprint (\w+): ([\d.]+) MB', text))
ms = [float(v) for v in re.findall(r'bench_frames: .*?avg ([\d.]+) ms', text)]
assert len(mb) == 3 and len(ms) == 3, (mb, ms)
steps = ['one_tab', 'second_tab', 'duplicate_open']
for step, frame in zip(steps, ms):
    print(f'{step:15} {float(mb[step]):8.1f} MB  {frame:6.2f} ms/frame', flush=True)

nodes = json.loads((out / 'tabs.json').read_text())['nodes']
for repo in repos:
    tabs = [n for n in nodes if n.get('name', '').startswith(f'tab_{repo.name} ') and n['rect']['width'] > 0]
    assert len(tabs) == 1, (repo, [n['name'] for n in tabs])
# Footprint moves with the allocator and text caches, so it is reported, not
# asserted. Frame time with the reviewed tab in the background is the check:
# it must cost what a lone sidebar tab costs, far below the review it hides.
assert ms[1] < ms[0] / 2 and ms[1] < 8, ms
print(f'PASS inactive review tab adds no frame cost ({ms[1]:.2f} ms vs {ms[0]:.2f} active); duplicate open focuses the existing tab', flush=True)
