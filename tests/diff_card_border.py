import argparse
import json
import os
from pathlib import Path
import subprocess

# Each file in the review is one bordered card: the border overlay starts at the
# file header and ends at the context footer, before and after scrolling.
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
out = parser.parse_args().output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / 'fixture'
repo.mkdir()

def git(*args):
    subprocess.check_output(['git', '-C', str(repo), *args], text=True)

for command in [('init', '-q', '-b', 'main'), ('config', 'user.name', 'Card fixture'),
                ('config', 'user.email', 'card@example.invalid'), ('config', 'commit.gpgsign', 'false')]:
    git(*command)
for name in ['a.txt', 'b.txt', 'c.txt']:
    (repo / name).write_text(''.join(f'{name} {i}\n' for i in range(1, 61)))
git('add', '.')
git('commit', '-qm', 'base')
for name in ['a.txt', 'b.txt', 'c.txt']:
    (repo / name).write_text(''.join(f'{name} {i}{" changed" if i % 12 == 5 else ""}\n' for i in range(1, 61)))

def check(label, open_review):
    for zoom in [100, 140, 200]:
        directory = out / label / str(zoom)
        directory.mkdir(parents=True)
        script = 'resize 1800 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        script += 'wait_for_refresh\n' + open_review + 'wait_for_refresh\nwait_frames 10\nscreenshot cards\n'
        script += 'hover_ui file_header_row\nscroll_wheel 0 -12\nwait_frames 20\nscreenshot scrolled\n'
        (directory / 'journey.e2e').write_text(script)
        with (directory / 'run.log').open('w') as log:
            result = subprocess.run([str(ROOT / 'output/floatinghotel.exe'), str(repo), '--test-mode', '--headless', f'--test-script={directory / "journey.e2e"}',
                f'--screenshot-dir={directory}', '--e2e-timeout=120'], cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS='1'), stdout=log, stderr=subprocess.STDOUT, timeout=180)
        assert result.returncode == 0, directory / 'run.log'
        footer_y = {}
        for shot in ['cards', 'scrolled']:
            nodes = json.loads((directory / f'{shot}.json').read_text())['nodes']
            named = lambda name: [n['rect'] for n in nodes if n.get('name') == name and n['rect']['width'] > 0]
            cards, headers, footers = named('file_card_border'), named('file_header_row'), named('file_context_footer')
            assert cards, (label, zoom, shot)
            for card in cards:
                assert any(abs(card['y'] + card['height'] - f['y'] - f['height']) <= 1 and abs(card['x'] - f['x']) <= 3 and
                           abs(card['width'] - f['width']) <= 30 for f in footers), (label, zoom, shot, card, footers)
            # Headers that start the cards on screen (a pinned header sits inside its card).
            for header in [h for h in headers if h['y'] < 1100]:
                assert any(c['y'] - 1 <= header['y'] <= c['y'] + c['height'] for c in cards), (label, zoom, shot, header, cards)
            footer_y[shot] = min(f['y'] for f in footers)
        assert footer_y['scrolled'] < footer_y['cards'] - 100, (label, zoom, footer_y)
        print(f'PASS {label} {zoom}% each file card border runs from its header to its footer ({len(cards)} cards after scroll)', flush=True)

# Working changes embed the diff in a section; a commit puts it straight in the scroll view.
check('working', 'hover_ui sidebar_mode_tabs\nscroll_wheel 0 -10\nwait_frames 3\nclick_text "Unstaged (3)"\n')
git('commit', '-qam', 'change')
check('commit', 'click_ui commit_row\n')
