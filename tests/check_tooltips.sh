#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tooltip_repo=$(mktemp -d /tmp/floatinghotel-tooltips.XXXXXX)
trap 'rm -rf "$tooltip_repo"' EXIT
git -C "$tooltip_repo" init -q -b feature/a-long-branch-name-that-must-remain-readable-in-a-tooltip
git -C "$tooltip_repo" config user.name 'UI test'
git -C "$tooltip_repo" config user.email 'ui-test@example.invalid'
mkdir -p "$tooltip_repo/src/a-very-long-directory-name/nested-path-for-tooltip-verification"
printf 'short\n' > "$tooltip_repo/short.cpp"
printf 'long\n' > "$tooltip_repo/a-very-long-filename-that-is-truncated-in-the-sidebar-but-fits-in-a-wide-source-header.cpp"
printf 'before\n' > "$tooltip_repo/src/a-very-long-directory-name/nested-path-for-tooltip-verification/long-file-name.txt"
git -C "$tooltip_repo" add .
git -C "$tooltip_repo" commit -qm 'A long commit subject that does not fit in the sidebar but should be fully readable when hovered, including the final words VISIBLE_END'
printf 'changed\n' >> "$tooltip_repo/short.cpp"
printf 'changed\n' >> "$tooltip_repo/a-very-long-filename-that-is-truncated-in-the-sidebar-but-fits-in-a-wide-source-header.cpp"
printf 'after\n' >> "$tooltip_repo/src/a-very-long-directory-name/nested-path-for-tooltip-verification/long-file-name.txt"
FH_NATIVE_MENUS=1 nice -n 10 output/floatinghotel.exe "$tooltip_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_47_tooltips.e2e \
  --screenshot-dir=output/screenshots/file-tooltips --e2e-timeout=40

nice -n 10 python3 - <<'PY_CHECK'
import json
from pathlib import Path

root = Path('output/screenshots/file-tooltips')
def snapshot(name):
    return json.loads((root / (name + '.json')).read_text())
def node(name, target):
    return next(n for n in snapshot(name)['nodes'] if n.get('name') == target and n['rendered'])

path = 'a-very-long-filename-that-is-truncated-in-the-sidebar-but-fits-in-a-wide-source-header.cpp'
wide = node('wide_file', 'jump_to_diff:' + path)
clipped = node('truncated_file', 'jump_to_diff:' + path)
assert wide['text'] == clipped['text'] == path
assert wide['rect']['width'] > 4 * clipped['rect']['width']
for name, zoom in [('truncated_file', 1), ('zoom140_file', 1.4), ('zoom200_file', 2)]:
    data = snapshot(name)
    assert abs(data['ui_scale'] - zoom) < .001
    hot = next(n for n in data['nodes'] if n['hot'])
    assert hot['name'] == 'commit_changed_file'
    r, p = hot['visible_rect'], data['pointer']
    assert r['x'] <= p['x'] < r['x'] + r['width']
    assert r['y'] <= p['y'] < r['y'] + r['height']
wide = node('wide_picker', 'file_picker_result')['rect']['width']
narrow = node('narrow_picker', 'file_picker_result')['rect']['width']
restored = node('widened_picker', 'file_picker_result')['rect']['width']
assert wide > narrow + 100
assert abs(wide - restored) < .1
print('PASS: truncation tooltips, resize restoration, source tabs, action tooltips, and zoom geometry')
PY_CHECK
