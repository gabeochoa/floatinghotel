import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
JOURNEYS = [
    'preview_tabs', 'document_titles', 'tab_overflow', 'close_tabs', 'reorder_tabs', 'recent_tabs', 'restore_tabs',
    'history_visits', 'source_origin', 'anchor_layout', 'anchor_pages',
    'focus_return', 'shortcut_focus', 'escape_dismissal',
    'tree_states', 'tree_keyboard', 'tree_type_select', 'tree_reveal', 'preserve_folds', 'review_tree_menu',
    'compact_tree', 'tree_refresh_position', 'commit_keyboard', 'history_pagination',
    'quick_open_overlay', 'quick_open_scope', 'quick_open_ranking', 'line_navigation',
    'retained_search', 'search_debounce', 'grouped_search', 'search_keyboard', 'search_comparison_origin',
    'document_find', 'source_find', 'source_find_cancel', 'source_header', 'bookmark_navigator',
    'commit_metadata', 'selected_file_review', 'hunk_chrome', 'change_navigation', 'local_context',
    'reader_caret', 'selection_gestures', 'keyboard_selection', 'copy_commands', 'selection_extent',
    'continuous_source', 'continuous_fragments', 'syntax_state', 'syntax_diff', 'source_folding', 'source_folding_large',
    'repeated_selection', 'loading_feedback', 'commit_prefetch',
    'dock_reading', 'dock_inbox', 'saved_review_return', 'follow_up_review', 'feedback_return',
    'triage_reading', 'triage_feedback', 'triage_repositories', 'triage_split_profile',
    'zoom_hover', 'container_hover', 'code_wrap', 'reading_width', 'window_restore', 'sidebar_footer', 'navigation_regressions',
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--stages', nargs='+')
    parser.add_argument('--skip-units', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = ROOT / 'output/floatinghotel.exe'
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    commands = [('units', ['bash', 'tests/run_unit_tests.sh'])]
    for name, native in [('reading', False), ('reading_native', True)]:
        command = [sys.executable, 'tests/reading_journey.py', '--output', str(out / name)]
        if native:
            command.append('--native')
        commands.append((name, command))
    commands += [(name, [sys.executable, f'tests/{name}.py', '--output', str(out / name)]) for name in JOURNEYS]
    commands += [
        ('cancel_buttons', [sys.executable, 'tests/loading_feedback.py', '--output', str(out / 'cancel_buttons'), '--cancel-buttons', '--zooms', '100']),
        ('navigation_boundary', [sys.executable, 'tests/check_navigation_boundary.py']),
        ('startup', ['bash', 'tests/check_startup_ready.sh', '--headless-timing']),
        ('native_window', ['bash', 'tests/check_native_window_resize.sh']),
        ('native_live_resize', ['bash', 'tests/check_native_live_resize.sh']),
    ]
    selected = args.stages or [name for name, _ in commands if not (args.skip_units and name == 'units')]
    unknown = set(selected) - {name for name, _ in commands}
    if unknown:
        parser.error('Unknown stages: ' + ', '.join(sorted(unknown)))
    metadata = dict(binary_sha256=digest, platform=platform.platform(), load_average=os.getloadavg(),
                    revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    stages=selected, skipped_units=args.skip_units,
                    test_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT / 'tests').glob('*.py')})
    (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    results = []
    env = dict(os.environ)
    env.pop('FH_NATIVE_MENUS', None)
    for name, command in commands:
        if name not in selected:
            continue
        if (out / 'STOP').exists():
            print('Stopped between stages; completed results are retained.', flush=True)
            raise SystemExit(2)
        assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest, 'Binary changed during replay'
        print('RUN ' + name, flush=True)
        started = time.monotonic()
        with (out / f'{name}.log').open('w') as log:
            process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            try:
                code = process.wait(timeout=1800)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                code = 124
        unchanged = hashlib.sha256(binary.read_bytes()).hexdigest() == digest
        results.append(dict(stage=name, command=command, exit_code=code, seconds=time.monotonic() - started,
                            binary_unchanged=unchanged, passed=code == 0 and unchanged))
        (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(('PASS ' if results[-1]['passed'] else 'FAIL ') + name, flush=True)
        if not unchanged:
            raise SystemExit('Binary changed during replay')
    if not all(row['passed'] for row in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
