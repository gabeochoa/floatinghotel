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
paths = ['src/lib/a.cpp', 'src/lib/b.cpp', 'src/z.cpp'] + [f'tail/f{i:03}.cpp' for i in range(60)]
for path in paths:
    target = repo / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('int value = 1;\n')
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Tree fixture'),
                ('config', 'user.email', 'tree@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Keyboard fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
for path in paths:
    (repo / path).write_text('int value = 2;\n')
initial_diff = subprocess.check_output(['git', '-C', str(repo), 'diff'])
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
results = {}
for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    checks = {}
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Keyboard fixture"\nwait_for_refresh\nclick_ui commit_file_filter\nkey TAB\n'
    def capture(name, path, count=2):
        checks[name] = path
        return f'wait_frames 6\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'
    script += capture('entry', 'src/')
    script += 'key RIGHT\n' + capture('directory', 'src/lib/')
    script += 'key RIGHT\n' + capture('first', 'src/lib/a.cpp')
    script += 'key DOWN\n' + capture('second', 'src/lib/b.cpp')
    script += 'key UP\nkey DOWN\n' + capture('cycled', 'src/lib/b.cpp')
    script += 'key LEFT\nkey LEFT\n' + capture('collapsed', 'src/lib/')
    script += 'key RIGHT\n' + capture('expanded', 'src/lib/')
    script += 'key RIGHT\n' + capture('entered_again', 'src/lib/a.cpp')
    script += 'key LEFT\nkey ENTER\n' + capture('enter_collapsed', 'src/lib/')
    script += 'key DOWN\n' + capture('skipped', 'src/z.cpp')
    script += 'key LEFT\nkey LEFT\nkey DOWN\nkey RIGHT\n' + capture('tail', 'tail/f000.cpp')
    script += 'key DOWN\n' * 45 + capture('far', 'tail/f045.cpp')
    script += 'key UP\n' * 45 + capture('return', 'tail/f000.cpp')
    script += 'key ENTER\n' + capture('kept', 'tail/f000.cpp')
    script += 'click_ui commit_file_filter\ntype "f001"\n' + capture('filter', None)
    script += 'key CMD+A\nkey BACKSPACE\nclick_ui review_unstaged_changes\nwait_for_refresh\nclick_ui commit_file_filter\nkey TAB\n'
    script += capture('working_entry', 'src/')
    script += 'key RIGHT\nkey RIGHT\n' + capture('working_file', 'src/lib/a.cpp')
    script += 'native_menu_action "Review Workspace (toggle)"\nclick_ui sidebar_working_files\nwait_frames 8\nnative_menu_action "Tree View"\nwait_frames 8\nclick_ui file_row\nkey DOWN\n' + capture('files_tree', 'src/lib/b.cpp')
    script += 'key LEFT\nkey LEFT\nkey DOWN\n' + capture('files_skipped', 'src/z.cpp')
    script += 'native_menu_action "Changed Files View"\nwait_frames 8\nclick_ui file_row\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey DOWN\nkey DOWN\nkey DOWN\n' + capture('files_flat', 'tail/f000.cpp')
    script += 'native_menu_action "All Files View"\nwait_for_refresh\nclick_ui file_row\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey UP\nkey DOWN\n' + capture('files_all', 'src/lib/b.cpp', 3)
    script += 'key ENTER\n' + capture('source_kept', 'src/lib/b.cpp', 3)
    script += 'click_ui content_document_2\nclick_ui sidebar_review\nwait_for_refresh\nwait_frames 30\nclick_text "Open file"\nwait_for_refresh\nwait_frames 8\nclick_ui commit_file_filter\nkey TAB\n' + capture('source_origin_entry', 'tail/f000.cpp', 4)
    script += 'key DOWN\nwait_for_refresh\nwait_frames 8\n' + capture('source_origin_tree', 'tail/f001.cpp', 4)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    geometry = {}
    def checkpoint(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())
    origin = checkpoint('source_origin_entry')
    source = next(tab for tab in origin['tabs'] if tab['id'] == origin['active'])
    assert source['path'].startswith('tail/f') and source['path'].endswith('.cpp'), source
    checks['source_origin_entry'] = source['path']
    checks['source_origin_tree'] = f"tail/f{int(source['path'][6:9]) + 1:03}.cpp"
    for name, expected in checks.items():
        layout = json.loads((directory / f'{name}.json').read_text())
        focused = [n for n in layout['nodes'] if n['focused'] and n['rendered'] and not n['hidden']]
        assert len(focused) == 1, (zoom, name, focused)
        node = focused[0]
        target = node['focus_target']
        assert target['region'] == 'Tree', (zoom, name, target)
        if expected is None:
            assert target['control'] == 'commit_file_filter', (zoom, name, target)
            continue
        assert target['item'] == expected, (zoom, name, target, expected)
        assert node['visual_focus'], (zoom, name, node)
        assert node['visible_rect']['height'] >= node['rect']['height'] - 1, (zoom, name, 'clipped focus', node)
        geometry[name] = {'focus': target, 'rect': node['rect'], 'visible': node.get('visible_rect')}
        rendered_rows = [n for n in layout['nodes'] if n['rendered'] and n.get('name') in ['commit_changed_file', 'file_row']]
        assert len(rendered_rows) < 40, (zoom, name, len(rendered_rows))
        headers = [n for n in layout['nodes'] if n['rendered'] and n.get('name') == 'file_header_label']
        assert len(headers) < 40, (zoom, name, 'offscreen header controls', len(headers))
    for name in ['first', 'second', 'cycled', 'skipped', 'far', 'return']:
        state = checkpoint(name)
        active = next(t for t in state['tabs'] if t['id'] == state['active'])
        assert active['preview'] and active['path'] == checks[name], (zoom, name, state)
    for name in ['kept', 'source_kept']:
        state = checkpoint(name)
        assert not next(t for t in state['tabs'] if t['id'] == state['active'])['preview'], (zoom, name)
    results[str(zoom)] = geometry
    assert subprocess.check_output(['git', '-C', str(repo), 'diff', '--cached']) == b''
    assert subprocess.check_output(['git', '-C', str(repo), 'diff']) == initial_diff
    print(f'PASS {zoom}%: tree arrows, collapsed descendants, preview/keep, distant focus, files modes', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, geometry=results), indent=2) + '\n')
