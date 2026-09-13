import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()
(repo / 'old.cpp').write_text(''.join(f'int value_{i} = {i};\n' for i in range(1, 101)))
(repo / 'gone.cpp').write_text(''.join(f'int removed_{i} = {i};\n' for i in range(1, 81)))

def git(*command):
    return subprocess.check_output(['git', '-C', str(repo), *command], text=True).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Source origin fixture'),
                ('config', 'user.email', 'source@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Original sources')]:
    git(*command)
before = git('rev-parse', 'HEAD')
git('mv', 'old.cpp', 'new.cpp')
git('rm', '-q', 'gone.cpp')
p = repo / 'new.cpp'
p.write_text(p.read_text().replace('value_50 = 50', 'value_50 = 5000'))
git('commit', '-qam', 'Rename and delete')
after = git('rev-parse', 'HEAD')
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name, count):
    return f'wait_frames 20\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


def active_visit(directory, name):
    w = json.loads((directory / f'{name}.workspace.json').read_text())
    return w['history'][w['history_index']]


def check_return(directory, source_name, return_name):
    anchor = active_visit(directory, source_name)['location']['source']['origin_anchor']
    data = json.loads((directory / f'{return_name}.json').read_text())
    viewport = next(n['rect'] for n in data['nodes'] if n.get('name') == 'commit_detail_scroll' and n['rendered'])
    rows = [r for r in data['reading_rows'] if r['path'] == anchor['path'] and r['line'] == anchor['line'] and r['sign'] == anchor['sign']]
    expected = viewport['y'] + viewport['height'] * anchor['fraction']
    assert rows and min(abs(r['rect']['y'] - expected) for r in rows) < 2, (anchor, rows, viewport)
    assert active_visit(directory, return_name)['location']['review'] == active_visit(directory, source_name)['location']['source']['origin']


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    initial = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    initial += 'native_menu_action "Zoom In"\n' * steps
    initial += 'click_text "Rename and delete"\nwait_for_refresh\nkey ENTER\nclick_ui "jump_to_diff:new.cpp"\n' + capture('origin', 2)

    def replay(name, script):
        target = directory / name
        target.mkdir()
        path = target / 'journey.e2e'
        path.write_text(script)
        with (target / 'run.log').open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
                f'--test-script={path}', f'--screenshot-dir={target}', '--e2e-timeout=120'], cwd=ROOT,
                env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=150)
        assert result.returncode == 0, target / 'run.log'
        return target

    probe = replay('probe', initial)
    data = json.loads((probe / 'origin.json').read_text())
    deleted = next(r for r in data['reading_rows'] if r['path'] == 'new.cpp' and r['line'] == 50 and r['sign'] == '-')
    x, y = deleted['content_x'] + 35, deleted['rect']['y'] + deleted['rect']['height'] / 2
    script = initial + f'click {x} {y}\n' + capture('selected', 2)
    script += 'click_text "Open file"\nwait_for_refresh\n' + capture('previous_source', 3)
    script += 'click_ui full_file_back\nwait_for_refresh\n' + capture('returned', 3)
    script += 'click_ui "jump_to_diff:gone.cpp"\n' + capture('deleted_origin', 3)
    script += 'click_text "Open previous file"\nwait_for_refresh\n' + capture('deleted_source', 3)
    script += 'click_ui full_file_back\nwait_for_refresh\n' + capture('deleted_return', 3)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    result = replay('journey', script)
    source = active_visit(result, 'previous_source')['location']['source']
    assert source['path'] == 'old.cpp' and source['revision'] == {'kind': 'object', 'value': before}, source
    assert source['line'] == 50 and source['column'] > 1, source
    source_layout = json.loads((result / 'previous_source.json').read_text())
    assert any(r['line'] == 50 and r['text'] == 'int value_50 = 50;' for r in source_layout['reading_rows'])
    assert not any('5000' in r['text'] for r in source_layout['reading_rows'])
    check_return(result, 'previous_source', 'returned')
    deleted_layout = json.loads((result / 'deleted_origin.json').read_text())
    viewport = next(n['rect'] for n in deleted_layout['nodes'] if n.get('name') == 'commit_detail_scroll' and n['rendered'])
    first = next(r for r in deleted_layout['reading_rows'] if r['path'] == 'gone.cpp' and r['sign'] == '-'
                 and r['rect']['y'] + r['rect']['height'] > viewport['y'] and r['rect']['y'] < viewport['y'] + viewport['height'])
    source = active_visit(result, 'deleted_source')['location']['source']
    assert source['path'] == 'gone.cpp' and source['revision'] == {'kind': 'object', 'value': before}, source
    assert source['line'] == first['line'], (source, first)
    check_return(result, 'deleted_source', 'deleted_return')
    assert active_visit(result, 'returned')['location']['review']['commit']['value'] == after
    print(f'PASS {zoom}%: selected deleted line and column, renamed old path, before object, first visible deleted line, and exact review return', flush=True)
assert git('status', '--porcelain') == ''
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + '\n')
