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
for name in 'abc':
    lines = [f'const char* {name}_{i} = "' + ('café readable source ' * 16 if i < 30 else 'short') + '";\n' for i in range(300)]
    (repo / f'{name}.cpp').write_text(''.join(lines))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'History fixture'),
                ('config', 'user.email', 'history@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Review lines')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
oid = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name, count):
    return f'wait_frames 20\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


def picker(path):
    return f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\nkey ENTER\nwait_for_refresh\n'


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Review lines"\nwait_for_refresh\nkey ENTER\nclick_ui "jump_to_diff:a.cpp"\nwait_frames 8\nhover_ui commit_detail_scroll\nscroll_wheel 0 -30\n' + capture('review_a', 2)
    script += 'click_ui "jump_to_diff:b.cpp"\nwait_frames 8\nhover_ui commit_detail_scroll\nscroll_wheel 0 -25\n' + capture('review_b', 2)
    script += 'key ALT+LEFT\nwait_for_refresh\n' + capture('back_a', 2)
    script += 'key ALT+RIGHT\nwait_for_refresh\n' + capture('forward_b', 2)
    script += 'key ALT+LEFT\nwait_for_refresh\nwait_frames 12\n'
    script += picker('a.cpp') + 'hover_ui diff_scroll\nscroll_wheel 0 -30\n' + capture('source_a', 3)
    script += picker('c.cpp') + 'hover_ui diff_scroll\nscroll_wheel 0 -25\n' + capture('source_c', 4)
    script += 'click_ui close_document_3\n' + capture('closed_a', 3)
    script += 'key ALT+LEFT\nwait_for_refresh\n' + capture('revisited_a', 4)
    script += 'key ALT+RIGHT\nwait_for_refresh\n' + capture('forward_c', 4)
    script += 'key ALT+LEFT\nwait_for_refresh\nwait_frames 12\n'
    script += picker('b.cpp') + capture('branch', 5)
    script += 'key ALT+RIGHT\nwait_for_refresh\n' + capture('no_forward', 5)
    script += 'hover_ui diff_scroll\nscroll_wheel 0 -27\n' + capture('scrolled', 5)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'

    def workspace(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())

    def visit(name):
        w = workspace(name)
        return w['history'][w['history_index']]

    def check_anchor(before, after):
        anchor = visit(before)['anchor']
        restored = visit(after)['anchor']
        assert restored['path'] == anchor['path'] and restored['revision'] == anchor['revision']
        data = json.loads((directory / f'{after}.json').read_text())
        viewport = next(n['rect'] for n in data['nodes'] if n.get('name') in ('diff_scroll', 'commit_detail_scroll') and n['rendered'])
        text = (repo / anchor['path']).read_text().splitlines()[anchor['line'] - 1]
        byte = len(text[:anchor['column'] - 1].encode())
        rows = [r for r in data['reading_rows'] if r['path'] == anchor['path'] and r['line'] == anchor['line']
                and r['offset'] <= byte < r['offset'] + len(r['text'].encode())]
        expected = viewport['y'] + viewport['height'] * anchor['fraction']
        assert rows and min(abs(r['rect']['y'] - expected) for r in rows) < 2, (zoom, before, after, anchor, rows, viewport)

    for before, after in [('review_a', 'back_a'), ('review_b', 'forward_b'), ('source_a', 'revisited_a'), ('source_c', 'forward_c')]:
        check_anchor(before, after)
    assert visit('back_a')['location']['review']['file'] == 'a.cpp'
    assert visit('forward_b')['location']['review']['file'] == 'b.cpp'
    assert visit('revisited_a')['location']['source']['origin']['file'] == 'a.cpp'
    assert visit('revisited_a')['location']['source']['origin']['commit']['value'] == oid
    assert workspace('review_a')['active'] == workspace('review_b')['active'] == 2
    assert workspace('source_c')['history'] == workspace('closed_a')['history']
    assert workspace('source_a')['active'] != workspace('revisited_a')['active']
    branch, no_forward, scrolled = [workspace(n) for n in ['branch', 'no_forward', 'scrolled']]
    assert branch['history_index'] == len(branch['history']) - 1
    assert branch['history_index'] == no_forward['history_index'] == scrolled['history_index']
    assert len(branch['history']) == len(no_forward['history']) == len(scrolled['history'])
    assert branch['active'] == no_forward['active'] == scrolled['active']
    assert visit('scrolled')['anchor'] != visit('no_forward')['anchor']
    assert all(workspace(n)['inactive_payloads_empty'] for n in ['back_a', 'forward_b', 'revisited_a', 'forward_c'])
    print(f'PASS {zoom}%: same-review file visits, wrapped Unicode anchors, closed-source revisit, origin, Forward truncation, and scrolling without new visits', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + '\n')
