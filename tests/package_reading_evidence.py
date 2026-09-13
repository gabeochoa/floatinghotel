import argparse
import gzip
from pathlib import Path
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--step', type=int, required=True)
parser.add_argument('--run', action='append', default=[])
parser.add_argument('--image', action='append', default=[])
args = parser.parse_args()
if not 1 <= args.step <= 60:
    parser.error('--step must be between 1 and 60')
output = ROOT / f'docs/reading-navigation-evidence/step{args.step:02d}'
output.mkdir(parents=True, exist_ok=True)

def entry(value):
    label, path = value.split('=', 1)
    if Path(label).name != label or label in ['', '.', '..']:
        parser.error('Evidence labels must be plain filenames')
    path = ROOT / path
    if not path.exists():
        parser.error(f'Missing evidence: {path}')
    return label, path

for value in args.run:
    label, source = entry(value)
    if not source.is_dir():
        parser.error(f'Run must be a directory: {source}')
    count = 0
    with tarfile.open(output / f'{label}.tar.gz', 'w:gz') as archive:
        for path in sorted(source.rglob('*')):
            relative = path.relative_to(source)
            if not path.is_file() or path.suffix not in ['.json', '.log', '.e2e']:
                continue
            if any(part == '.git' or 'fixture' in part or 'settings' in part for part in relative.parts[:-1]):
                continue
            archive.add(path, arcname=str(relative))
            count += 1
    print(f'{label}: {count} evidence files')

for value in args.image:
    label, source = entry(value)
    if source.suffix != '.png' or not label.endswith('.png'):
        parser.error('Images must be PNG files')
    shutil.copyfile(source, output / label)

for path in sorted((ROOT / 'output').glob(f'step{args.step:02d}*.log')):
    with gzip.open(output / f'{path.name}.gz', 'wb') as archive:
        archive.write(path.read_bytes())
print(output)
