import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
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
    (repo / f'{name}.cpp').write_text(''.join(f'int original_{name}_{i} = {i};\n' for i in range(1200)))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Restore fixture'),
                ('config', 'user.email', 'restore@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Original sources')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
original = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
(repo / 'a.cpp').write_text((repo / 'a.cpp').read_text().replace('original_a_10 = 10', 'original_a_10 = 1000'))
subprocess.run(['git', '-C', str(repo), 'commit', '-qam', 'Later source'], check=True, capture_output=True)
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def picker(path, keep=True):
    action = 'key ENTER' if keep else 'click_ui file_picker_result'
    return f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\n{action}\nwait_for_refresh\n'


def anchor_visible(directory, name, anchor):
    data = json.loads((directory / f'{name}.json').read_text())
    nodes = [n for n in data['nodes'] if n['rendered'] and not n['hidden']]
    viewport = next(n['rect'] for n in nodes if n.get('name') in ('diff_scroll', 'commit_detail_scroll'))
    candidates = []
    for n in nodes:
        if n.get('name') != 'diff_line': continue
        text = n.get('text', '')
        if re.match(r'^\s*' + str(anchor['line']) + r'\s', text): candidates.append(n['rect'])
    assert candidates, (directory, name, anchor, 'Saved line is not visible')
    expected = viewport['y'] + viewport['height'] * anchor['fraction']
    assert min(abs(r['y'] - expected) for r in candidates) < 2, (directory, name, anchor, candidates, viewport)


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / 'settings'
    settings.mkdir()

    def replay(name, script):
        target = directory / name
        target.mkdir()
        path = target / 'journey.e2e'
        path.write_text(script)
        with (target / 'run.log').open('w') as log:
            result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
                f'--test-script={path}', f'--screenshot-dir={target}', '--e2e-timeout=180'], cwd=ROOT,
                env=dict(os.environ, FH_NATIVE_MENUS='1', FH_TEST_SETTINGS_DIR=str(settings)), stdout=log, stderr=subprocess.STDOUT, timeout=210)
        assert result.returncode == 0, target / 'run.log'
        return target

    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Later source"\nwait_for_refresh\nkey ENTER\nclick_text "Original sources"\nwait_for_refresh\nkey ENTER\n'
    script += 'click_ui open_full_file\nwait_for_refresh\nkey ENTER\nhover_ui diff_scroll\nscroll_wheel 0 -30\nwait_frames 20\nscreenshot historical_a\n'
    script += 'click_ui full_file_back\nwait_for_refresh\nhover_ui commit_detail_scroll\nscroll_wheel 0 -40\nwait_frames 20\nscreenshot review\n'
    script += picker('b.cpp') + 'hover_ui diff_scroll\nscroll_wheel 0 -25\nwait_frames 20\nscreenshot working_b\n'
    script += picker('c.cpp', False) + 'wait_frames 10\nworkspace_checkpoint 6 preview\nscreenshot preview\nsave_window_state\n'
    saved_dir = replay('save', script)
    saved = json.loads((settings / 'settings.json').read_text())
    session = saved['reading_sessions'][str(repo)]
    assert len(session['documents']) == 5 and session['active'] == 4, session
    a, review, b = session['documents'][3], session['documents'][2], session['documents'][4]
    assert a['location']['source']['revision'] == {'kind': 'object', 'value': original}
    assert a['location']['source']['origin']['commit']['value'] == original
    assert all('c.cpp' != d['location'].get('source', {}).get('path') for d in session['documents'])
    for document in [a, review, b]:
        assert document['anchor']['line'] > 1 and document['anchor']['fraction'] >= 0
    shutil.copy(settings / 'settings.json', directory / 'saved-settings.json')
    (repo / 'a.cpp').write_text(''.join(f'int working_a_{i} = {i};\n' for i in range(1200)))
    script = 'reading_probe cold source b.cpp -\nkey F12\nwait_for_refresh\nwait_frames 12\nreading_checkpoint\nworkspace_checkpoint 5 cold\nscreenshot cold\n'
    script += 'native_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * steps
    script += f'click_ui open_tabs_menu\nwait_frames 3\nclick_ui "context_menu_item_a.cpp · {original[:7]}"\nwait_for_refresh\nwait_frames 12\nscreenshot restored_a\nworkspace_checkpoint 5 restored_a\n'
    script += 'click_ui open_tabs_menu\nwait_frames 3\nclick_ui "context_menu_item_Original sources"\nwait_for_refresh\nwait_frames 12\nscreenshot restored_review\nworkspace_checkpoint 5 restored_review\nbench_frames 120\nexpect_p99_below 20\n'
    restored = replay('restore', script)
    cold = json.loads((restored / 'cold.reading.json').read_text())
    assert cold['blob_cache']['misses'] <= 1 and cold['patch_cache']['misses'] <= 1, ('Inactive tabs loaded during startup', cold)
    assert cold['source_path'] == 'b.cpp' and cold['source_revision'] == ''
    anchor_visible(restored, 'cold', b['anchor'])
    anchor_visible(restored, 'restored_a', a['anchor'])
    anchor_visible(restored, 'restored_review', review['anchor'])
    for name, active in [('cold', 5), ('restored_a', 4), ('restored_review', 3)]:
        workspace = json.loads((restored / f'{name}.workspace.json').read_text())
        assert workspace['active'] == active and workspace['inactive_payloads_empty']
        assert all(not tab['preview'] for tab in workspace['tabs'])
    data = json.loads((restored / 'restored_a.json').read_text())
    assert any('original_a_' in n.get('text', '') for n in data['nodes'])
    assert not any('working_a_' in n.get('text', '') for n in data['nodes'])
    missing = json.loads(json.dumps(saved))
    session = missing['reading_sessions'][str(repo)]
    missing_doc = json.loads(json.dumps(a))
    missing_doc['location']['source']['revision'] = {'kind': 'object', 'value': 'f' * 40}
    missing_doc.pop('anchor', None)
    session['documents'].append(missing_doc)
    session['active'] = 5
    (settings / 'settings.json').write_text(json.dumps(missing))
    unavailable = replay('missing', 'wait_for_refresh\nwait_frames 12\nassert_ui full_file_error hidden=false\nworkspace_checkpoint 6 unavailable\nscreenshot unavailable\n')
    workspace = json.loads((unavailable / 'unavailable.workspace.json').read_text())
    assert workspace['active'] == 6 and workspace['tabs'][-1]['revision'] == 'f' * 40
    data = json.loads((unavailable / 'unavailable.json').read_text())
    assert not any(n.get('name') == 'diff_line' and n['rendered'] for n in data['nodes'])
    missing_review = json.loads(json.dumps(saved))
    session = missing_review['reading_sessions'][str(repo)]
    session['documents'].append({'location': {'review': {'kind': 'commit', 'commit': {'kind': 'object', 'value': 'f' * 40}, 'file': ''}}, 'subject': 'Unavailable review', 'recent': 1})
    session['active'] = 5
    (settings / 'settings.json').write_text(json.dumps(missing_review))
    unavailable_review = replay('missing-review', 'wait_for_refresh\nwait_frames 12\nassert_ui commit_load_error hidden=false\nworkspace_checkpoint 6 unavailable_review\nscreenshot unavailable_review\n')
    workspace = json.loads((unavailable_review / 'unavailable_review.workspace.json').read_text())
    assert workspace['active'] == 6 and workspace['tabs'][-1]['revision'] == 'f' * 40
    unresolved = json.loads(json.dumps(saved))
    session = unresolved['reading_sessions'][str(repo)]
    pending = json.loads(json.dumps(a))
    pending['location']['source']['revision'] = {'kind': 'query', 'value': 'HEAD'}
    pending.pop('anchor', None)
    session['documents'].append(pending)
    session['active'] = 5
    (settings / 'settings.json').write_text(json.dumps(unresolved))
    pending_dir = replay('unresolved', 'wait_for_refresh\nwait_frames 12\nassert_ui saved_revision_unavailable hidden=false\nworkspace_checkpoint 6 pending\nscreenshot pending\nclick_ui resolve_saved_revision\nwait_for_refresh\nwait_frames 12\nworkspace_checkpoint 6 resolved\nscreenshot resolved\n')
    pending_workspace = json.loads((pending_dir / 'pending.workspace.json').read_text())
    assert pending_workspace['active'] == 6 and pending_workspace['tabs'][-1]['revision'] == 'HEAD'
    pending_layout = json.loads((pending_dir / 'pending.json').read_text())
    assert not any(n.get('name') == 'diff_line' and n['rendered'] for n in pending_layout['nodes'])
    resolved_workspace = json.loads((pending_dir / 'resolved.workspace.json').read_text())
    current_head = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
    assert resolved_workspace['active'] == 6 and resolved_workspace['tabs'][-1]['revision'] == current_head
    print(f'PASS {zoom}% saved / default zoom restored: kept tabs, preview exclusion, active fallback, lazy cache activity, exact logical source/review anchors, immutable historical bytes, unavailable source/review objects, explicit unresolved-ref resolution', flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + '\n')
