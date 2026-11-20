import json
import pathlib
import sys


checked = 0
targets = 0
# Standalone controls need a real hover/click box, not just a glyph-sized
# one. Dense list rows are intentionally 24px and are not listed here.
minimum_target = {
    "approve_hunk_btn": 24,
    "comment_hunk_btn": 24,
    "comment_add_btn": 28,
    "expand_context_above": 24,
    "expand_context_below": 24,
    "hunk_folded_marker": 28,
    "review_display_mode": 28,
    "branch_switcher": 24,
}
for filename in sys.argv[1:]:
    snapshot = json.loads(pathlib.Path(filename).read_text())
    scale = snapshot.get("ui_scale", 1)
    nodes = [node for node in snapshot["nodes"] if node["rendered"]]
    for node in nodes:
        name = node.get("name", "")
        if name in ("tab_add", "tab_close", "zoom_in", "zoom_out") or name.startswith(("menu_header_", "menu_item_", "fold_file:", "jump_to_diff:")):
            padding = node["padding"]
            assert padding["top"] + padding["bottom"] <= node["rect"]["height"], (filename, name, padding)
            assert padding["left"] + padding["right"] <= node["rect"]["width"], (filename, name, padding)
            checked += 1
        minimum = minimum_target.get(name)
        if name.startswith("fold_file:"):
            minimum = 28
        if minimum is not None:
            height = node["visible_rect"]["height"] / scale
            assert height + 0.1 >= minimum, (filename, name, height, minimum)
            targets += 1
    subject = [node for node in nodes if node.get("name") == "commit_detail_subject"]
    metadata = [node for node in nodes if node.get("name") == "commit_meta_compact"]
    if subject and metadata:
        assert abs(subject[0]["rect"]["x"] - metadata[0]["rect"]["x"]) < 0.1, filename
assert checked, "No controls checked"
print(f"PASS {checked} control boxes have usable content space; {targets} hover targets meet their minimum height; commit headings align")
