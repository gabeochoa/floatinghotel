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
(repo / 'a.cpp').write_text(''.join(f'int value_{i} = {i};\n' for i in range(80)))
for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Focus fixture'),
                ('config', 'user.email', 'focus@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                ('add', '.'), ('commit', '-qm', 'Focus fixture')]:
    subprocess.run(['git', '-C', str(repo), *command], check=True, capture_output=True)
p = repo / 'a.cpp'
p.write_text(p.read_text().replace('value_10 = 10', 'value_10 = 1000'))
initial_diff = subprocess.check_output(['git', '-C', str(repo), 'diff'])
binary = ROOT / 'output/floatinghotel.exe'
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name, count=2):
    return f'wait_frames 8\nworkspace_checkpoint {count} {name}\nscreenshot {name}\n'


for zoom, steps in [(100, 0), (140, 4), (200, 10)]:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Focus fixture"\nwait_for_refresh\nkey ENTER\nclick_ui content_document_2\n' + capture('before')
    script += 'key ESCAPE\nkey ESCAPE\n' + capture('after')
    script += 'key CMD+P\nwait_frames 3\ntype "a.cpp"\nwait_for_refresh\nkey ENTER\nwait_for_refresh\n' + capture('source', 3)
    script += 'key ESCAPE\nkey ESCAPE\n' + capture('source_retained', 3)
    script += 'key CMD+F\nwait_frames 3\ntype "value"\nkey CMD+P\nwait_frames 3\n' + capture('nested', 3)
    script += 'key ESCAPE\n' + capture('picker_closed', 3)
    script += 'key ESCAPE\nkey ESCAPE\n' + capture('find_closed', 3)
    script += 'click_ui open_tabs_menu\nwait_frames 3\nkey ESCAPE\n' + capture('menu_closed', 3)
    script += 'key CMD+SHIFT+F\nwait_frames 3\ntype "value"\nclick_ui repo_search_submit\nwait_for_refresh\nwait_frames 12\nclick_ui repo_search_preview\n' + capture('search_preview', 3)
    script += 'key ESCAPE\n' + capture('preview_closed', 3)
    script += 'key ESCAPE\nkey ESCAPE\n' + capture('search_closed', 3)
    script += 'click_ui review_unstaged_changes\nwait_for_refresh\nwait_frames 6\nclick_ui comment_hunk_btn\nwait_frames 4\nclick_ui comment_input\ntype "Escape keeps this draft"\n' + capture('draft', 3)
    script += 'key ESCAPE\n' + capture('composer_closed', 3)
    script += 'click_ui comment_hunk_btn\nwait_frames 4\n' + capture('draft_restored', 3)
    script += 'click_ui comment_add_btn\nwait_frames 12\n' + capture('feedback', 3)
    script += 'key ESCAPE\nkey ESCAPE\n' + capture('feedback_closed', 3)
    script += 'click_ui content_document_3\nwait_for_refresh\nhover_ui diff_scroll\nscroll_wheel 0 -30\n' + capture('expanded_before', 3)
    script += 'native_menu_action "Collapse reading panel"\n' + capture('collapsed', 3)
    script += 'key ESCAPE\n' + capture('collapsed_escape', 3)
    script += 'native_menu_action "Expand reading panel"\n' + capture('expanded_after', 3)
    script += 'native_menu_action "Collapse reading panel"\nwait_frames 5\nclick_ui commit_row\nwait_for_refresh\n' + capture('row_expands', 3)
    script += 'native_menu_action "Keyboard Shortcuts"\nwait_frames 5\nscreenshot shortcuts\nkey ESCAPE\n' + capture('shortcuts_closed', 3)
    script += 'resize 900 800\nwait_frames 8\nnative_menu_action "Keyboard Shortcuts"\nwait_frames 6\nscreenshot shortcuts_narrow\nkey ESCAPE\nresize 1800 1100\nwait_frames 8\n'
    script += 'native_menu_action "Compare Revisions..."\n' + capture('comparison_editor', 3)
    script += 'key ESCAPE\n' + capture('comparison_cancelled', 3)
    script += 'native_menu_action "Compare Revisions..."\nclick_ui compare_base\nkey CMD+A\ntype "HEAD"\nclick_ui compare_target\nkey CMD+A\ntype "HEAD"\nclick_ui compare_submit\nwait_for_refresh\n' + capture('comparison', 4)
    script += 'key ESCAPE\nkey ESCAPE\n' + capture('comparison_retained', 4)
    script += 'key CMD+W\nwait_frames 5\nclick_ui content_document_2\nwait_for_refresh\nkey CMD+W\n' + capture('document_closed', 2)
    script += 'key ESCAPE\nkey ESCAPE\n' + capture('closed_stays_closed', 2)
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless',
            f'--test-script={path}', f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def checkpoint(name):
        return json.loads((directory / f'{name}.workspace.json').read_text())

    def visible(name, control):
        return [n for n in json.loads((directory / f'{name}.json').read_text())['nodes'] if n.get('name') == control and n['rendered'] and not n['hidden'] and n['visible_rect']['width'] > 0 and n['visible_rect']['height'] > 0]

    def identities(value):
        return [{k: v for k, v in tab.items() if k != 'find'} for tab in value['tabs']]

    for before_name, after_name in [('before', 'after'), ('source', 'source_retained'), ('nested', 'picker_closed'), ('nested', 'find_closed'), ('nested', 'menu_closed'), ('nested', 'search_closed'), ('draft', 'composer_closed'), ('feedback', 'feedback_closed'), ('expanded_before', 'collapsed'), ('expanded_before', 'collapsed_escape'), ('expanded_before', 'expanded_after'), ('row_expands', 'shortcuts_closed'), ('document_closed', 'closed_stays_closed'), ('comparison_editor', 'comparison_cancelled'), ('comparison', 'comparison_retained')]:
        before, after = checkpoint(before_name), checkpoint(after_name)
        assert before['active'] == after['active'] and before['history_index'] == after['history_index'] and identities(before) == identities(after), (zoom, after_name, 'Dismissal changed document')
    assert visible('picker_closed', 'diff_find_input') and not visible('picker_closed', 'file_picker_input'), zoom
    assert not visible('find_closed', 'diff_find_input') and visible('find_closed', 'full_file_header'), zoom
    assert visible('preview_closed', 'repo_search_input') and not visible('preview_closed', 'repo_search_preview_close'), zoom
    assert not visible('search_closed', 'repo_search_input') and visible('search_closed', 'full_file_header'), zoom
    assert not checkpoint('composer_closed')['review']['composing'] and not visible('composer_closed', 'comment_input'), zoom
    assert checkpoint('draft_restored')['review']['draft'] == 'Escape keeps this draft', zoom
    assert checkpoint('feedback_closed')['review']['comments'] == 1 and not visible('feedback_closed', 'basket_close'), zoom
    assert not visible('collapsed', 'content_tabs') and not visible('collapsed_escape', 'content_tabs'), zoom
    before = checkpoint('expanded_before')
    after = checkpoint('expanded_after')
    before_anchor = before['history'][before['history_index']]['anchor']
    after_anchor = after['history'][after['history_index']]['anchor']
    for key in ['path', 'revision', 'line', 'column', 'side']:
        assert before_anchor[key] == after_anchor[key], (zoom, key, before_anchor, after_anchor)
    assert abs(before_anchor['fraction'] - after_anchor['fraction']) < .003, (zoom, before_anchor, after_anchor)
    layout = json.loads((directory / 'expanded_after.json').read_text())
    viewport = visible('expanded_after', 'diff_scroll')[0]['rect']
    expected_y = viewport['y'] + viewport['height'] * before_anchor['fraction']
    rows = [r for r in layout['reading_rows'] if r['path'] == before_anchor['path'] and r['line'] == before_anchor['line']]
    assert rows and min(abs(row['rect']['y'] - expected_y) for row in rows) < 2, (zoom, before_anchor, viewport, rows)
    assert visible('expanded_after', 'full_file_header') and visible('row_expands', 'commit_subject'), zoom
    assert not visible('comparison_cancelled', 'compare_base') and visible('comparison_cancelled', 'commit_subject'), zoom
    for name, width, height in [('shortcuts', 1800, 1100), ('shortcuts_narrow', 900, 800)]:
        modal = visible(name, 'modal')
        close = visible(name, 'shortcuts_close')
        assert len(modal) == 1 and close, (zoom, name)
        for node in [modal[0], close[0]]:
            rect = node['rect']
            assert rect['x'] >= 0 and rect['y'] >= 0 and rect['x'] + rect['width'] <= width + 1 and rect['y'] + rect['height'] <= height + 1, (zoom, name, rect)
    assert visible('shortcuts', 'shortcut_reference') and not visible('shortcuts_closed', 'shortcut_reference'), zoom
    assert len(checkpoint('document_closed')['tabs']) == 2 and all(t['id'] != 2 for t in checkpoint('document_closed')['tabs']), zoom
    print(f'PASS {zoom}%: nested dismissal, retained documents, drafts, explicit panel collapse, close shortcut', flush=True)
assert subprocess.check_output(['git', '-C', str(repo), 'diff', '--cached']) == b''
assert subprocess.check_output(['git', '-C', str(repo), 'diff']) == initial_diff
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps(dict(passed=True, binary_sha256=digest), indent=2) + '\n')
