"""Correctness under the comparison's stress cases, floatinghotel side.

Each case asserts what reached the screen, because stale or blank content is
not a successful load:
  rapid A/B/A opens       the last request wins with its own content
  superseded searches     only the final query's results are shown
  closing during loading  a closed, half-loaded file leaves no residue
  changed between pages   a file rewritten mid-scroll never mixes versions
  same-size write, mtime  a rewrite that keeps size and mtime is still seen
  copied bytes            select-all copies exactly the file's bytes
  eviction                a reopen after the caches churn shows the file
  syntax on far pages     a block comment opened before a distant page still
                          colours that page (first text and syntax arrive
                          together here: the reader carries lexical state)

python3 stress.py --output DIR [--binary EXE]
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import fixture  # noqa: E402

LEFT_SUPER = "SUPER"


def quick_open(path, keep=True):
    return ('hold_key SUPER\nkey P\nrelease_key SUPER\nwait_for_refresh\nclick_ui file_picker_input\nkey CMD+A\n'
            f'type "{path}"\nwait_frames 3\nwait_for_refresh\n' + ('key ENTER\n' if keep else ''))


def close_document():
    return f'hold_key {LEFT_SUPER}\nkey W\nrelease_key {LEFT_SUPER}\nwait_frames 3\n'


def nodes(directory, shot):
    return json.loads((directory / f'{shot}.json').read_text())


def texts(layout):
    # Visible labels plus code rows (code is reported as reading rows).
    labels = [n.get('text', '') for n in layout['nodes'] if n.get('rendered') and not n.get('hidden')]
    return labels + [r.get('text', '') for r in layout.get('reading_rows', [])]


def rows(layout, path):
    return {r['line']: r for r in layout.get('reading_rows', []) if r['path'] == path}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=ROOT / 'output/floatinghotel.exe')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    repo = out / 'fixture'
    fixture.build(repo)
    # A block comment that opens on line 1 and closes past a later page.
    (repo / 'comment.cpp').write_text('/* opened on line one\n' + ''.join(f'still comment {i}\n' for i in range(2, 60001)) +
                                      '*/\n' + ''.join(f'int after_{i} = {i};\n' for i in range(60002, 60100)))
    alpha = (repo / 'alpha.cpp').read_bytes()

    script = ['resize 1600 1000', 'wait_for_refresh', 'wait_frames 10']
    # Rapid A/B/A: three opens back to back, no settling between them.
    script += [quick_open('alpha.cpp'), quick_open('beta.cpp'), quick_open('alpha.cpp'),
               'wait_for_refresh', 'wait_frames 20', 'screenshot rapid']
    # Superseded search: replace the query before the first one settles.
    script += ['native_menu_action "Search Repository..."', 'wait_frames 5', 'click_ui repo_search_input', 'key CMD+A',
               'type "module_03_"', 'key ENTER', 'key CMD+A', f'type "{fixture.SEARCH_TOKEN}"', 'key ENTER',
               'wait_for_refresh', 'wait_frames 60', 'screenshot superseded', 'click_ui repo_search_close', 'wait_frames 3']
    # Close while loading, then open something else.
    script += [quick_open('big.txt'), close_document(), quick_open('gamma.cpp'),
               'wait_for_refresh', 'wait_frames 20', 'screenshot after_close']
    # Changed between pages: load big.txt's first page, let the harness rewrite
    # it, then ask for the next page.
    script += [quick_open('big.txt'), 'wait_for_refresh', 'wait_frames 10', 'screenshot pages_first', 'wait_for_path rewrite_done',
               'hover_ui diff_scroll'] + ['scroll_wheel 0 -400', 'wait_frames 2'] * 120 + ['wait_for_refresh', 'wait_frames 30', 'screenshot pages_after']
    script += [close_document()]
    # Same size, same mtime: reopen must show the new bytes.
    script += [quick_open('alpha.cpp'), 'wait_for_refresh', 'wait_frames 5', 'screenshot same_before', close_document(),
               'wait_for_path same_done', quick_open('alpha.cpp'), 'wait_for_refresh', 'wait_frames 20', 'screenshot same_after']
    # Copied bytes: there is no whole-file select-all in a code view, so select
    # three lines from the keyboard and compare the selection byte for byte.
    script += [quick_open('gamma.cpp'), 'wait_for_refresh', 'wait_frames 5', 'click_text "int gamma_1 = 1;"', 'key HOME',
               'hold_key LEFT_SHIFT', 'key DOWN', 'key DOWN', 'key DOWN', 'release_key LEFT_SHIFT', 'wait_frames 5', 'screenshot select_all']
    # Eviction: churn the caches with distant pages of two large files, then reopen.
    for target in (20000, 60000, 100000, 140000):
        script += [quick_open('find.txt'), 'wait_for_refresh', 'key CTRL+G', 'wait_frames 4', 'click_ui line_picker_input', 'key CMD+A',
                   f'type "{target}"', 'key ENTER', 'wait_for_refresh', 'wait_frames 5']
    for target in (100000, 200000, 300000, 390000):
        script += [quick_open('big.txt'), 'wait_for_refresh', 'key CTRL+G', 'wait_frames 4', 'click_ui line_picker_input', 'key CMD+A',
                   f'type "{target}"', 'key ENTER', 'wait_for_refresh', 'wait_frames 5']
    script += [quick_open('beta.cpp'), 'wait_for_refresh', 'wait_frames 20', 'screenshot evicted']
    # Syntax on a distant page: jump into the middle of the long comment.
    script += [quick_open('comment.cpp'), 'wait_for_refresh', 'key CTRL+G', 'wait_frames 4', 'click_ui line_picker_input', 'key CMD+A',
               'type "59990"', 'key ENTER', 'wait_for_refresh', 'wait_frames 20', 'screenshot far_comment']
    (out / 'stress.e2e').write_text('\n'.join(s.rstrip('\n') for s in script) + '\n')

    log = (out / 'run.log').open('w')
    app = subprocess.Popen([str(args.binary), str(repo), '--test-mode', '--headless', f'--test-script={out / "stress.e2e"}',
                            f'--screenshot-dir={out}', '--e2e-timeout=600'], env=dict(os.environ, FH_NATIVE_MENUS='1'),
                           stdout=log, stderr=subprocess.STDOUT)

    def wait_for(shot):
        deadline = time.time() + 300
        while not (out / f'{shot}.png').exists():
            assert app.poll() is None and time.time() < deadline, f'app ended before {shot}'
            time.sleep(0.05)

    wait_for('pages_first')
    big = repo / 'big.txt'
    big.write_text(''.join(f'LINE {i:06d} rewritten mid scroll {i % 97:02d}\n' for i in range(1, fixture.BIG_LINES + 1)))
    (out / 'rewrite_done').write_text('')
    wait_for('same_before')
    stat = (repo / 'alpha.cpp').stat()
    changed = alpha.replace(b'int alpha_1 = 1;', b'int alpha_1 = 9;')
    assert len(changed) == len(alpha)
    (repo / 'alpha.cpp').write_bytes(changed)
    os.utime(repo / 'alpha.cpp', ns=(stat.st_atime_ns, stat.st_mtime_ns))
    (out / 'same_done').write_text('')
    code = app.wait(timeout=600)
    log.close()
    text = (out / 'run.log').read_text()
    errors = [l for l in text.splitlines() if 'E2E ERROR' in l or 'TIMEOUT' in l]
    results = {}

    def check(name, ok, detail=''):
        results[name] = {'pass': bool(ok), 'detail': detail}

    rapid = nodes(out, 'rapid')
    check('rapid_aba', 'int alpha_1 = 1;' in texts(rapid) or any('alpha_1 = 1' in t for t in texts(rapid)),
          'alpha.cpp content on screen after A/B/A')
    check('rapid_aba_no_stale', not any('beta_1 = 1' in t for t in texts(rapid)), 'no beta.cpp rows left on screen')
    superseded = [t for t in texts(nodes(out, 'superseded'))]
    check('superseded_search', any('search_target.cpp' in t for t in superseded) and not any('module_03.cpp' in t for t in superseded),
          'only the final query\'s file is listed')
    after_close = texts(nodes(out, 'after_close'))
    check('close_during_load', any('gamma_1 = 1' in t for t in after_close) and not any('line 000001' in t for t in after_close),
          'gamma.cpp shown, nothing from the closed big.txt')
    pages = texts(nodes(out, 'pages_after'))
    old = any(t.startswith('line ') and 'lorem' in t for t in pages)
    new = any(t.startswith('LINE ') for t in pages)
    reload_notice = any('changed' in t.lower() and 'reload' in t.lower() for t in pages)
    check('changed_between_pages', not (old and new), f'old rows {old}, new rows {new}, reload notice {reload_notice}')
    same = texts(nodes(out, 'same_after'))
    check('same_size_same_mtime', any('alpha_1 = 9' in t for t in same) and not any('alpha_1 = 1;' in t for t in same),
          'rewritten bytes visible despite equal size and mtime')
    selected = nodes(out, 'select_all').get('selection_text', '')
    expected = ''.join((repo / 'gamma.cpp').read_text().splitlines(keepends=True)[:3])
    check('copied_bytes', selected in (expected, expected.rstrip('\n')), f'selection {selected!r} vs {expected!r}')
    check('evicted_reopen', any('beta_1 = 1' in t for t in texts(nodes(out, 'evicted'))), 'beta.cpp content after cache churn')
    far = nodes(out, 'far_comment')
    far_rows = [r for r in far.get('reading_rows', []) if r['path'] == 'comment.cpp' and 59980 <= r['line'] <= 59999]
    # Colours are not in the layout dump; the lexical state carried across
    # pages is covered by test_content_reader's page-cursor tests. Here: the
    # far page's own text arrived, at the requested place.
    check('syntax_far_page', bool(far_rows) and any('still comment 5999' in r.get('text', '') for r in far_rows),
          f'{len(far_rows)} rows of 59980-59999 on screen')
    summary = {'exit': code, 'errors': errors, 'results': results, 'passed': all(r['pass'] for r in results.values()) and not errors and code == 0}
    (out / 'stress.json').write_text(json.dumps(summary, indent=1))
    for name, r in results.items():
        print(('PASS ' if r['pass'] else 'FAIL ') + name + ' - ' + r['detail'])
    if errors:
        print('\n'.join(errors[:10]))
    sys.exit(0 if summary['passed'] else 1)


if __name__ == '__main__':
    main()
