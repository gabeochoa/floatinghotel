import argparse
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--reading', type=Path, required=True)
parser.add_argument('--native-reading', type=Path, required=True)
parser.add_argument('--suite', type=Path, required=True)
parser.add_argument('--retry-suite', type=Path, action='append', default=[])
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()


def distribution(values):
    values = sorted(values)
    return dict(count=len(values), minimum=values[0], p95=values[math.ceil(len(values) * .95) - 1], maximum=values[-1])


def summarize(rows):
    groups = []
    for zoom, temperature, step in sorted({(r['zoom'], r['temperature'], r['step']) for r in rows}):
        group = [r for r in rows if (r['zoom'], r['temperature'], r['step']) == (zoom, temperature, step)]
        groups.append(dict(zoom=zoom, temperature=temperature, step=step,
                           selection_ms=distribution([r['selection_ms'] for r in group]),
                           ready_ms=distribution([r['ready_ms'] for r in group]),
                           cache_hits=sum(r['blob_cache']['hit_delta'] + r['patch_cache']['hit_delta'] for r in group)))
    warm = [r for r in rows if r['temperature'] == 'warm' and r['step'] in ['commit', 'source', 'back']]
    return dict(samples=len(rows), groups=groups,
                selection_50ms_pass=all(r['selection_ms']['p95'] <= 50 for r in groups),
                warm_switch_100ms_pass=all(r['ready_ms']['p95'] <= 100 for r in groups
                                         if r['temperature'] == 'warm' and r['step'] in ['commit', 'source', 'back']),
                warm_switch_samples=warm,
                owned_content_bytes_max=max(r['owned_content_bytes'] for r in rows),
                blob_cache_bytes_max=max(r['blob_cache']['bytes'] for r in rows),
                patch_cache_bytes_max=max(r['patch_cache']['bytes'] for r in rows))


attempts = [args.suite, *args.retry_suite]
stages = [dict(row, attempt=str(directory)) for directory in attempts
          for row in json.loads((directory / 'results.json').read_text())]
latest = {row['stage']: row for row in stages}
metadata = [json.loads((directory / 'metadata.json').read_text()) for directory in attempts]
complete = set(metadata[0]['stages']).issubset(latest)
same_binary = len({row['binary_sha256'] for row in metadata}) == 1
result = dict(method='Nearest-rank sample p95; three repeats per zoom and step are not a population p95. CPU frame completion excludes OS presentation.',
              memory_scope='Owned content capacity estimate, including source pages and lexical state. Excludes tab metadata, allocator overhead, workers, GPU, and RSS. Caches reported separately.',
              baseline=summarize(json.loads((ROOT / 'docs/reading-navigation-evidence/samples.json').read_text())),
              final=summarize(json.loads((args.reading / 'samples.json').read_text())),
              native=summarize(json.loads((args.native_reading / 'samples.json').read_text())),
              stages=stages, latest_stage_results=list(latest.values()), stages_complete=complete,
              same_binary=same_binary,
              latest_stages_passed=complete and same_binary and all(row['passed'] for row in latest.values()))
bench = re.compile(r'bench_frames: (\d+) frames, (\d+) entities: avg ([\d.]+) ms\s+p50 ([\d.]+)\s+p99 ([\d.]+)\s+max ([\d.]+)')
frames = []
failures = []
for directory in attempts:
    for path in sorted(directory.rglob('*.log')):
        if any('fixture' in part or 'settings' in part for part in path.relative_to(directory).parts[:-1]):
            continue
        for line in path.read_text(errors='replace').splitlines():
            if match := bench.search(line):
                count, entities, average, median, p99, maximum = match.groups()
                frames.append(dict(attempt=str(directory), log=str(path.relative_to(directory)), frames=int(count), entities=int(entities),
                                   average_ms=float(average), median_ms=float(median), p99_ms=float(p99), maximum_ms=float(maximum)))
            if any(marker in line for marker in ['E2E ERROR', '[TIMEOUT]', '(FAIL)', 'AssertionError', 'TimeoutExpired']):
                failures.append(dict(attempt=str(directory), log=str(path.relative_to(directory)), message=line))
result['render_samples'] = frames
result['reported_failures'] = failures
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(f'{len(frames)} render samples, {len(failures)} reported failures; warm-switch target passed: {result["native"]["warm_switch_100ms_pass"]}')
