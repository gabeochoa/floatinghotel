"""floatinghotel side of the px0 comparison: the same journeys as
px0_driver.mjs, timed by the app's reading probe from input dispatch to the
frame that shows the destination (and, where given, the destination line).

python3 fh_driver.py --binary EXE --repo DIR --out FILE [--reps N] [--zoom 100]
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fixture  # noqa: E402

LEFT_SUPER, LEFT_CONTROL = "SUPER", "LEFT_CONTROL"


def quick_open(path):
    return ('hold_key SUPER\nkey P\nrelease_key SUPER\nwait_for_refresh\n'
            f'click_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\nwait_for_refresh\n')


DEFAULT_CONFIG = {'a': 'alpha.cpp', 'b': 'beta.cpp', 'big': 'big.txt', 'readme': 'README.md', 'tree': True, 'find': True, 'search': True,
                  'extras': True}


def script(reps, zoom, width, height, cfg=None):
    cfg = {**DEFAULT_CONFIG, **(cfg or {})}
    lines = [f'resize {width} {height}', 'wait_for_refresh', 'native_menu_action "Reset Zoom"']
    lines += ['native_menu_action "Zoom In"'] * {100: 0, 140: 4, 200: 10}[zoom]
    if cfg['tree']:
        lines += ['native_menu_action "File Explorer"', 'wait_for_refresh', 'wait_frames 10']

    def probe(label, kind, path, line=None):
        return f'reading_probe {label} {kind} {path} -' + (f' {line}' if line else '')

    a, b, big, readme = cfg['a'], cfg['b'], cfg['big'], cfg['readme']
    for rep in range(1, reps + 1):
        def step(name, kind, path, action, line=None):
            label = f'r{rep}_{name}'
            lines.extend([probe(label, kind, path, line), action, 'reading_checkpoint', f'screenshot {label}'])

        documents = 3  # a, b and big; optional steps add theirs
        if cfg['tree']:
            # Revealing earlier files scrolls the explorer; start each rep at its top.
            lines += ['mouse_move 140 400', 'scroll_wheel 0 3000', 'wait_frames 10']
            step('tree_open', 'source', a, f'click_text "{a.split("/")[-1]}"')
        else:
            lines.append(quick_open(a).rstrip('\n'))
            step('quick_open_a', 'source', a, 'key ENTER')
        lines.append(quick_open(b).rstrip('\n'))
        step('quick_open', 'source', b, 'key ENTER')
        switch = f'hold_key {LEFT_CONTROL}\nkey TAB\nrelease_key {LEFT_CONTROL}'
        step('tab_a', 'source', a, switch)
        step('tab_b', 'source', b, switch)
        step('tab_a_again', 'source', a, switch)
        step('back', 'source', b, 'key ALT+LEFT')
        if cfg.get('extras'):
            step('forward', 'source', a, 'key ALT+RIGHT')
        lines.append(quick_open(big).rstrip('\n'))
        step('open_big', 'source', big, 'key ENTER')
        if cfg.get('extras'):
            documents += 1
            goto = lambda line: ['key CTRL+G', 'wait_frames 4', 'click_ui line_picker_input', 'key CMD+A', f'type "{line}"', 'wait_frames 3']
            lines += goto(300000)
            step('goto_line_far', 'source', big, 'key ENTER', 300000)
            # The reader carries lexical state into each page, so the comment
            # colour arrives with the first text; only first text is timed.
            lines.append(quick_open('comment.cpp').rstrip('\n'))
            lines += ['key ENTER', 'wait_for_refresh', 'wait_frames 5'] + goto(fixture.FAR_COMMENT_LINE)
            step('far_page_text', 'source', 'comment.cpp', 'key ENTER', fixture.FAR_COMMENT_LINE)
        if cfg['find']:
            documents += 1
            lines.append(quick_open('find.txt').rstrip('\n'))
            lines += ['key ENTER', 'wait_for_refresh', 'wait_frames 5', f'hold_key {LEFT_SUPER}', 'key F', f'release_key {LEFT_SUPER}', 'wait_frames 3',
                      'click_ui diff_find_input', 'key CMD+A', f'type "{fixture.needle(fixture.NEEDLE_LINE)[:-1]}"', 'wait_frames 10',
                      # The prefix matches the decoy; let that jump finish first.
                      f'reading_probe r{rep}_decoy source find.txt - {fixture.DECOY_LINE}', 'key RIGHT', 'reading_checkpoint']
            step('find_later_page', 'source', 'find.txt', f'type "{fixture.needle(fixture.NEEDLE_LINE)[-1]}"', fixture.NEEDLE_LINE)
            lines += ['key ESCAPE', 'wait_frames 3']
        if cfg['search']:
            documents += 1
            lines += ['native_menu_action "Search Repository..."', 'wait_frames 5',
                      'click_ui repo_search_input', 'key CMD+A', f'type "{fixture.SEARCH_TOKEN}"', 'key ENTER',
                      'wait_for_refresh', 'wait_frames 30']
            step('search_to_line', 'source', fixture.SEARCH_FILE, 'click_ui repo_search_result', fixture.SEARCH_LINE)
            lines += ['click_ui repo_search_close', 'wait_frames 3']
        if readme:
            documents += 1
            lines.append(quick_open(readme).rstrip('\n'))
            lines += ['key ENTER', 'wait_for_refresh', 'wait_frames 5', 'click_ui full_file_options', 'wait_frames 3']
            step('markdown_preview', 'markdown', readme, 'click_text "Preview Markdown"')
        # Close this repetition's source documents; the review document stays.
        lines += [f'hold_key {LEFT_SUPER}\nkey W\nrelease_key {LEFT_SUPER}', 'wait_frames 3'] * (documents - (1 if a == big else 0))
    # 1,000 frames on the large file, a long scroll before each 100.
    lines.append(quick_open(big).rstrip('\n'))
    lines += ['key ENTER', 'wait_for_refresh', 'wait_frames 10', 'hover_ui diff_scroll']
    lines += ['scroll_wheel 0 -2000\nbench_frames 100'] * 10
    lines += ['log_footprint end']
    return '\n'.join(lines) + '\n'


def memory_script(zoom, width, height):
    """20 tabs, a long scroll, close everything, then idle. The idle gates are
    files the caller creates on a wall clock (headless waits are frame ticks)."""
    lines = [f'resize {width} {height}', 'wait_for_refresh', 'native_menu_action "Reset Zoom"']
    lines += ['native_menu_action "Zoom In"'] * {100: 0, 140: 4, 200: 10}[zoom]
    lines += ['wait_for_path mark_start']
    for i in range(fixture.TAB_FILES):
        path = f'src/module_{i:02d}.cpp'
        lines += [quick_open(path).rstrip('\n'), f'reading_probe tab{i:02d} source {path} -', 'key ENTER', 'reading_checkpoint']
    lines += ['screenshot tabs_open', 'wait_for_path mark_tabs_open']
    lines += [quick_open('big.txt').rstrip('\n'), 'reading_probe big source big.txt -', 'key ENTER', 'reading_checkpoint',
              'hover_ui diff_scroll'] + ['scroll_wheel 0 -400', 'wait_frames 1'] * 200
    lines += ['wait_frames 30', 'screenshot scrolled', 'wait_for_path mark_scrolled']
    lines += [f'hold_key {LEFT_SUPER}\nkey W\nrelease_key {LEFT_SUPER}', 'wait_frames 3'] * (fixture.TAB_FILES + 1)
    lines += ['wait_frames 30', 'screenshot closed', 'wait_for_path mark_closed']
    lines += [f'wait_for_path idle_{i}' for i in range(1, 5)] + ['log_footprint idle']
    return '\n'.join(lines) + '\n'


def run_memory(binary, repo, work, zoom, width, height):
    """Run the memory journey, sampling RSS, and return the timeline."""
    import time
    import memsample
    work.mkdir(parents=True, exist_ok=False)
    (work / 'journey.e2e').write_text(memory_script(zoom, width, height))
    log = (work / 'run.log').open('w')
    app = subprocess.Popen([str(binary), str(repo), '--test-mode', '--headless', f'--test-script={work / "journey.e2e"}',
                            f'--screenshot-dir={work}', '--e2e-timeout=900'], env=dict(os.environ, FH_NATIVE_MENUS='1'),
                           stdout=log, stderr=subprocess.STDOUT)
    sampler = memsample.Sampler([app.pid]).start()
    marks = {}
    def gate(name, wait_for=None):
        # Each phase gets a settle second before its mark is released.
        time.sleep(1)
        marks[name] = time.time()
        (work / f'mark_{name}').write_text('')
    gate('start')
    for name, shot in (('tabs_open', 'tabs_open'), ('scrolled', 'scrolled'), ('closed', 'closed')):
        deadline = time.time() + 600
        while not (work / f'{shot}.png').exists() and app.poll() is None and time.time() < deadline:
            time.sleep(0.1)
        gate(name)
    # 30 s idle in four gates, each inside wait_for_path's 10 s bound.
    for i in range(1, 5):
        time.sleep(7.5)
        (work / f'idle_{i}').write_text('')
    marks['idle'] = time.time()
    code = app.wait(timeout=120)
    samples = sampler.stop()
    log.close()
    text = (work / 'run.log').read_text()
    errors = [line for line in text.splitlines() if 'E2E ERROR' in line or 'TIMEOUT' in line]
    import re
    footprint = [float(v) for v in re.findall(r'footprint idle: ([\d.]+) MB', text)]
    return {'app': 'floatinghotel', 'exit': code, 'errors': errors, 'marks': marks, 'samples': samples,
            'footprint_idle_mb': footprint[-1] if footprint else None}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--reps', type=int, default=3)
    parser.add_argument('--zoom', type=int, default=100, choices=[100, 140, 200])
    parser.add_argument('--width', type=int, default=1600)
    parser.add_argument('--height', type=int, default=1000)
    parser.add_argument('--journey', default='navigation', choices=['navigation', 'memory'])
    parser.add_argument('--config', type=Path, help='journey files and optional steps (default: the fixture)')
    args = parser.parse_args()
    if args.journey == 'memory':
        result = run_memory(args.binary, args.repo, args.out.with_suffix(''), args.zoom, args.width, args.height)
        args.out.write_text(json.dumps(result))
        sys.exit(1 if result['errors'] or result['exit'] else 0)
    work = args.out.with_suffix('')
    work.mkdir(parents=True, exist_ok=False)
    cfg = json.loads(args.config.read_text()) if args.config else None
    (work / 'journey.e2e').write_text(script(args.reps, args.zoom, args.width, args.height, cfg))
    with (work / 'run.log').open('w') as log:
        result = subprocess.run([str(args.binary), str(args.repo), '--test-mode', '--headless', f'--test-script={work / "journey.e2e"}',
                                 f'--screenshot-dir={work}', '--e2e-timeout=600'], env=dict(os.environ, FH_NATIVE_MENUS='1'),
                                stdout=log, stderr=subprocess.STDOUT, timeout=900)
    text = (work / 'run.log').read_text()
    errors = [line for line in text.splitlines() if 'E2E ERROR' in line or 'TIMEOUT' in line]
    samples = []
    for path in sorted(work.glob('r*_*.reading.json')):
        row = json.loads(path.read_text())
        rep, step = row['label'].split('_', 1)
        rep = int(rep[1:])
        samples.append({'app': 'floatinghotel', 'rep': rep, 'temperature': 'cold' if rep == 1 else 'warm', 'zoom': args.zoom,
                        'step': step, 'ms': row['ready_ms'], 'selection_ms': row['selection_ms']})
    import re
    for label in re.findall(r'Reading destination did not render: (r\d+_\w+)', text):
        rep, step = label.split('_', 1)
        rep = int(rep[1:])
        samples.append({'app': 'floatinghotel', 'rep': rep, 'temperature': 'cold' if rep == 1 else 'warm', 'zoom': args.zoom,
                        'step': step, 'ms': None, 'error': 'did not render'})
    footprint = [float(v) for v in re.findall(r'footprint end: ([\d.]+) MB', text)]
    frames = [{'avg': float(a), 'p99': float(p), 'max': float(m)} for a, p, m in
              re.findall(r'bench_frames: 100 frames.*?avg ([\d.]+) ms\s+p50 [\d.]+\s+p99 ([\d.]+)\s+max ([\d.]+)', text)]
    args.out.write_text(json.dumps({'app': 'floatinghotel', 'zoom': args.zoom, 'width': args.width, 'height': args.height,
                                    'reps': args.reps, 'exit': result.returncode, 'errors': errors,
                                    'footprint_mb': footprint[-1] if footprint else None, 'frames': frames,
                                    'samples': samples}, indent=1))
    if errors or result.returncode:
        print('\n'.join(errors[:10]), file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
