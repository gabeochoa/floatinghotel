import json
import math
from pathlib import Path
import sys

root = Path(sys.argv[1])


def snapshot(name):
    value = json.loads((root / f"{name}.json").read_text())
    assert (root / f"{name}.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    return value, [n for n in value["nodes"] if n["rendered"] and not n["hidden"]]


def named(nodes, name):
    return next(n for n in nodes if n.get("name") == name)


_, unstaged = snapshot("unstaged_review")
assert named(unstaged, "working_review_heading")["text"] == "Unstaged changes · 5 files"
paths = {n.get("text") for n in unstaged if n.get("name", "").startswith("jump_to_diff:")}
assert "TODO.md" in paths and "icon.png" in paths
assert ".gitignore" not in paths
_, feedback = snapshot("unstaged_feedback")
assert any("Please check this before committing." in n.get("text", "") for n in feedback)
_, staged = snapshot("staged_review")
assert named(staged, "working_review_heading")["text"] == "Staged changes · 1 file"
assert named(staged, "file_header_label")["text"] == ".gitignore"
assert not any(n.get("name", "").startswith("jump_to_diff:") and n.get("text") in paths for n in staged)
_, compact = snapshot("compact_commit")
assert named(compact, "commit_heading")["rect"]["height"] <= 60.1
assert named(compact, "commit_detail_subject")["font_size"]["value"] == 20

base, base_nodes = snapshot("text_default")
larger, larger_nodes = snapshot("text_larger")
reset, reset_nodes = snapshot("text_reset")
for value in (base, larger, reset):
    assert value["ui_scale"] == 1
for name in ("main_content", "content_tabs", "sidebar_bg", "status_bar_bg"):
    original = named(base_nodes, name)["rect"]
    assert named(larger_nodes, name)["rect"] == original, name
    assert named(reset_nodes, name)["rect"] == original, name
for nodes, expected in ((base_nodes, 17.6), (larger_nodes, 18.6), (reset_nodes, 17.6)):
    assert any(n.get("font") == "mono" and math.isclose(n["font_size"]["value"], expected, abs_tol=.001)
               and n["visible_rect"]["height"] > 0 for n in nodes)
for label, count in (("tab_context", 4), ("tabs_right_closed", 3), ("tab_reopened", 4), ("tab_keyboard_closed", 3)):
    _, nodes = snapshot(label)
    tabs = [n for n in nodes if n.get("name", "").startswith("content_document_")]
    assert len(tabs) == count, (label, len(tabs))
    for tab in tabs:
        close = named(nodes, "close_document_" + tab["name"].split("_")[-1])
        assert close["visible_rect"]["width"] >= close["rect"]["width"] - .1
        assert close["visible_rect"]["height"] >= close["rect"]["height"] - .1
print("PASS unstaged/staged separation, feedback, compact heading, independent text size, tab menus and closure")
