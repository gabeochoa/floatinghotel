"""Deterministic repository both apps read in the px0 comparison.

Every byte is generated, so two machines (or two runs) compare the same work:
small sources, a 400,000-line file below 64 MiB, a Find target below px0's
8 MiB search cap, long lines, Unicode, CRLF,
Markdown with links, and tokens at known lines for Find and repository search.
"""
import os
from pathlib import Path
import subprocess

BIG_LINES = 400_000            # opening and scrolling a 400k-line file
FIND_LINES, NEEDLE_LINE = 150_000, 120_000  # find.txt stays under px0's 8 MiB search cap
DECOY_LINE = 100
COMMENT_LINES, FAR_COMMENT_LINE = 60_000, 59_990  # comment.cpp: one block comment, then code
SEARCH_TOKEN = 'unique_search_token_42'
SEARCH_FILE, SEARCH_LINE = 'src/search_target.cpp', 77
SOURCES = ('alpha.cpp', 'beta.cpp', 'gamma.cpp')
TAB_FILES = 20


def needle(line):
    return f'needle_{line:06d}'


def build(path: Path) -> str:
    """Create the fixture at `path` and return its commit id."""
    path.mkdir(parents=True)
    env = dict(os.environ, GIT_AUTHOR_DATE='2026-01-01T00:00:00Z', GIT_COMMITTER_DATE='2026-01-01T00:00:00Z')

    def git(*args):
        return subprocess.run(['git', '-C', str(path), *args], env=env, check=True, capture_output=True, text=True).stdout.strip()

    for name in SOURCES:
        stem = name.split('.')[0]
        (path / name).write_text(''.join(f'int {stem}_{i} = {i};\n' for i in range(1, 181)))
    with (path / 'big.txt').open('w') as out:
        for i in range(1, BIG_LINES + 1):
            out.write(f'line {i:06d} lorem ipsum dolor sit amet {i % 97:02d}\n')
    # The decoy on line 100 matches every prefix of the needle but not the
    # needle itself, so the last keystroke of a find-as-you-type query has to
    # move the match from line 100 to a far page.
    with (path / 'find.txt').open('w') as out:
        for i in range(1, FIND_LINES + 1):
            out.write(f'{needle(i)} lorem ipsum dolor sit amet {i % 97:02d}\n' if i == NEEDLE_LINE
                      else f'{needle(NEEDLE_LINE)[:-1]}x decoy {i % 97:02d}\n' if i == DECOY_LINE
                      else f'line {i:06d} lorem ipsum dolor sit amet {i % 97:02d}\n')
    (path / 'long.txt').write_text(''.join(f'{i:03d} ' + ('long_' * 4000) + '\n' for i in range(1, 51)))
    (path / 'unicode.txt').write_text(''.join(f'{i:03d} λx → 東京 ✓ שלום 🙂 naïve café\n' for i in range(1, 201)))
    (path / 'crlf.txt').write_bytes(b''.join(f'crlf line {i}\r\n'.encode() for i in range(1, 201)))
    (path / 'src').mkdir()
    (path / SEARCH_FILE).write_text(''.join(
        (f'int found = 0; // {SEARCH_TOKEN}\n' if i == SEARCH_LINE else f'int filler_{i} = {i};\n') for i in range(1, 201)))
    for i in range(TAB_FILES):  # the 20-tab memory journey; each opens a fresh document
        (path / 'src' / f'module_{i:02d}.cpp').write_text(
            '/* module header\n   spanning lines */\n' + ''.join(f'int module_{i:02d}_{n} = {n};\n' for n in range(1, 401)))
    (path / 'comment.cpp').write_text('/* opened on line one\n' + ''.join(f'still comment {i}\n' for i in range(2, COMMENT_LINES + 1)) +
                                      '*/\n' + ''.join(f'int after_{i} = {i};\n' for i in range(1, 50)))
    (path / 'docs').mkdir()
    (path / 'docs/guide.md').write_text('# Guide\n\nBack to the [readme](../README.md).\n\n' +
                                        ''.join(f'Guide paragraph {i}.\n\n' for i in range(1, 80)))
    (path / 'README.md').write_text('# Reading fixture\n\nSee the [guide](docs/guide.md) and [alpha](alpha.cpp).\n\n' +
                                    ''.join(f'- item {i} with `code` and *emphasis*\n' for i in range(1, 120)))
    for args in (('init', '-q', '-b', 'main'), ('config', 'user.name', 'Compare fixture'),
                 ('config', 'user.email', 'compare@example.invalid'), ('config', 'commit.gpgsign', 'false'),
                 ('add', '.'), ('commit', '-qm', 'Comparison fixture')):
        git(*args)
    assert (path / 'big.txt').stat().st_size < 64 * 1024 * 1024
    assert (path / 'find.txt').stat().st_size < 8 * 1024 * 1024
    return git('rev-parse', 'HEAD')


if __name__ == '__main__':
    import sys
    print(build(Path(sys.argv[1])))
