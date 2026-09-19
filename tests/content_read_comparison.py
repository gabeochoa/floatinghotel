import argparse
import hashlib
import json
import math
import platform
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--before', type=Path, required=True)
parser.add_argument('--after', type=Path, required=True)
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--path', required=True)
parser.add_argument('--revision', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
samples = []
identities = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in [('before', args.before), ('after', args.after)]}
for block in range(4):
    for name in (['before', 'after'] if block % 2 == 0 else ['after', 'before']):
        binary = getattr(args, name).resolve()
        start = time.time()
        result = subprocess.run([str(binary), str(args.repo.resolve()), args.path, args.revision], text=True, capture_output=True, timeout=180)
        (args.output / f'{block}-{name}.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (block, name, result.stderr)
        rows = [line.split() for line in result.stdout.splitlines() if line.startswith('READ ')]
        assert len(rows) == 31
        for row in rows:
            _, iteration, duration, size, rev_parse, cat_file, ls_tree, ls_files = row
            samples.append(dict(block=block, variant=name, block_start_unix=start, iteration=int(iteration), ms=float(duration),
                                size=int(size), commands=dict(rev_parse=int(rev_parse), cat_file=int(cat_file), ls_tree=int(ls_tree), ls_files=int(ls_files))))
        print(f'Completed block {block} {name}', flush=True)
assert len({sample['size'] for sample in samples}) == 1
summary = {}
for name in ['before', 'after']:
    warm = [s for s in samples if s['variant'] == name and s['iteration'] > 0]
    values = sorted(s['ms'] for s in warm)
    summary[name] = dict(n=len(values), p50_ms=values[math.ceil(.5 * len(values)) - 1], p95_ms=values[math.ceil(.95 * len(values)) - 1], max_ms=values[-1],
                         command_counts=[dict(zip(['rev_parse', 'cat_file', 'ls_tree', 'ls_files'], counts)) for counts in sorted({tuple(s['commands'].values()) for s in warm})])
    assert hashlib.sha256(getattr(args, name).read_bytes()).hexdigest() == identities[name]
report = dict(summary=summary, samples=samples, binary_sha256=identities, machine=platform.platform(),
              method='Four alternating before/after blocks, same fixture and reader build optimization. First read in each process kept separately; OS caches not flushed. Timings include metadata freshness checks. Host activity is uncontrolled.')
(args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(summary, indent=2))
