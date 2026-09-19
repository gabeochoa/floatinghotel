import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
parser.add_argument('--zooms', nargs='+', type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*arguments):
    return subprocess.check_output(['git', '-C', str(repo), *arguments], text=True, stderr=subprocess.STDOUT).strip()

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Reading fixture'),
                ('config', 'user.email', 'reader@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
(repo / 'a.cpp').write_text(''.join(f'int value{i} = {i};\n' for i in range(1, 31)))
git('add', '.')
git('commit', '-qm', 'Reading baseline')
git('tag', '-a', 'v1-reader', '-m', 'First reader release')
(repo / 'a.cpp').write_text((repo / 'a.cpp').read_text().replace('value2 = 2', 'value2 = 20'))
git('commit', '-qam', 'Improve reader')
head = git('rev-parse', 'HEAD')
git('branch', 'topic-reader')
git('update-ref', 'refs/remotes/server/topic-reader', head)
(repo / 'a.cpp').write_text((repo / 'a.cpp').read_text().replace('value12 = 12', 'value12 = 120'))
(repo / 'draft.txt').write_text('Untracked stash draft\n')
git('stash', 'push', '-u', '-m', 'Saved reader draft')
(repo / 'a.cpp').write_text((repo / 'a.cpp').read_text().replace('value3 = 3', 'value3 = 30').replace('value13 = 13', 'value13 = 130'))
worktree = out / 'linked reader'
git('worktree', 'add', '-b', 'linked-reader', str(worktree))
before = git('status', '--porcelain')
binary = args.binary.resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()

def capture(name):
    return f'wait_frames 3\nwait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nscreenshot {name}\n'

def query(value):
    return 'click_ui file_picker_input\nkey CMD+A\n' + (f'type "{value}"\n' if value else 'key BACKSPACE\n') + 'wait 1\n'

for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1800 1200\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'click_text "Improve reader"\nwait_for_refresh\nkey ENTER\nkey CMD+P\nwait 1\n'
    script += query('topic-reader') + 'click_ui quick_open_category_Branches\n' + capture('branches')
    script += 'click_ui quick_open_category_Tags\n' + capture('empty_tags')
    script += query('v1-reader') + capture('tags')
    script += 'right_click_ui file_picker_result\nwait_frames 4\n' + capture('tag_menu')
    script += 'click_ui "context_menu_item_Compare tag with current review"\n' + capture('tag_comparison')
    script += 'key CMD+P\nwait 1\n' + query(head[:10]) + 'click_ui quick_open_category_Commits\n' + capture('hash')
    script += 'key ENTER\n' + capture('hash_review')
    script += 'click_ui commit_meta_toggle\n' + capture('metadata')
    script += 'key ESCAPE\nwait_frames 10\nkey CMD+P\nwait 1\n' + query('')
    script += 'click_ui quick_open_more\nwait_frames 3\nclick_ui context_menu_item_Reflog\nwait 1\n' + capture('reflog')
    script += 'click_ui quick_open_more\nwait_frames 3\nclick_ui context_menu_item_Stashes\nwait 1\n' + capture('stashes')
    script += 'key ENTER\nwait_frames 4\n' + capture('stash_portions')
    script += 'click_ui "context_menu_item_Review Working portion"\n' + capture('stash_review')
    script += 'key CMD+P\nwait 1\n' + query('') + 'click_ui quick_open_more\nwait_frames 3\nclick_ui context_menu_item_Worktrees\nwait 2\n' + capture('worktrees')
    script += query('linked-reader') + capture('worktree_match') + 'key ENTER\n' + capture('worktree_opened')
    script += 'click_ui "tab_fixture (main)"\n' + capture('worktree_return')
    script += 'key CMD+P\nwait_for_refresh\n' + query('')
    script += 'click_ui quick_open_more\nwait_frames 3\nclick_ui "context_menu_item_Git diagnostics"\nwait 2\n' + capture('git_runtime')
    script += 'key ESCAPE\nwait_frames 8\nclick_ui history_branch\nwait_frames 3\nclick_ui "context_menu_item_All refs"\n' + capture('all_refs')
    script += 'click_ui history_section_toggle\n' + capture('history_collapsed')
    script += 'click_ui history_section_toggle\n' + capture('history_restored')
    script += 'click_ui history_branch\nwait_frames 3\nclick_ui "context_menu_item_Current branch"\nwait_for_refresh\nclick_text "Improve reader"\nwait_for_refresh\nhold_key 340\nclick_text "Reading baseline"\nrelease_key 340\nwait_for_refresh\nright_click_ui commit_row\nwait_frames 4\n' + capture('range_menu')
    script += 'click_ui "context_menu_item_Review selected range"\n' + capture('range_review')
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / 'journey.e2e'
    path.write_text(script)
    with (directory / 'run.log').open('w') as log:
        result = subprocess.run([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={path}',
            f'--screenshot-dir={directory}', '--e2e-timeout=180'], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / 'run.log'
    def nodes(name, debug):
        snapshot = json.loads((directory / f'{name}.json').read_text())
        return [node for node in snapshot['nodes'] if node.get('name') == debug and node['rendered']]
    def text(name, debug):
        return ' '.join(node.get('text', '') for node in nodes(name, debug))
    assert 'topic-reader' in text('branches', 'file_picker_result')
    assert not nodes('empty_tags', 'file_picker_result')
    assert 'v1-reader' in text('tags', 'file_picker_result')
    assert head[:7] in text('hash', 'file_picker_result')
    assert nodes('metadata', 'commit_meta_box') and nodes('metadata', 'commit_details_scroll')
    assert 'Reading fixture' in text('reflog', 'file_picker_result')
    assert 'Saved reader draft' in text('stashes', 'file_picker_result')
    assert nodes('stash_portions', 'context_menu_item_Review Untracked portion')
    assert 'linked-reader' in text('worktrees', 'file_picker_result')
    assert 'git version' in text('git_runtime', 'file_picker_result')
    assert nodes('range_menu', 'context_menu_item_Review selected range')
    assert nodes('range_review', 'comparison_mode')
    assert '+30' in text('range_review', 'file_additions')
    assert text('worktree_opened', 'repo_header_label') == 'linked reader'
    assert text('worktree_return', 'repo_header_label') == 'fixture'
    assert not nodes('history_collapsed', 'commit_subject')
    assert nodes('history_restored', 'commit_subject')
    print(f'PASS {zoom}% triaged reading journeys', flush=True)
assert git('status', '--porcelain') == before
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / 'result.json').write_text(json.dumps({'passed': True, 'binary_sha256': digest}, indent=2) + '\n')
