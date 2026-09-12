import json
import sys
from pathlib import Path


for filename in sys.argv[1:]:
    snapshot = json.loads(Path(filename).read_text())
    scale = snapshot["ui_scale"]
    nodes = snapshot["nodes"]
    by_name = {node["name"]: node for node in nodes if node.get("name") and node["rendered"]}
    title = by_name["file_header_label"]
    assert title["font"] == "mono", title
    assert title["text"] == "src/plugins/ui/validation_systems.h", title
    assert len(title["text_spans"]) == 2, title
    assert "file_additions" in by_name and "file_deletions" in by_name
    assert "viewed_checkbox" in by_name
    actions = by_name["file_header_btns"]
    by_id = {node["id"]: node for node in nodes}
    for key in actions["children"]:
        rect = by_id[key]["rect"]
        parent = actions["rect"]
        assert rect["x"] >= parent["x"] - 0.1, by_id[key]
        assert rect["x"] + rect["width"] <= parent["x"] + parent["width"] + 0.1, by_id[key]
        assert rect["y"] + rect["height"] <= parent["y"] + parent["height"] + 0.1, by_id[key]
    for name in ("open_full_file", "approve_file_btn", "copy_file_diff_btn"):
        assert by_name[name]["rect"]["height"] / scale >= 28, by_name[name]
    if "folded" in Path(filename).stem:
        assert "hunk_header_label" not in by_name
        assert "diff_reading_end" in by_name
        print(f"PASS {filename}: folding hides the hunk and retains file actions and review progress")
        continue
    hunk = by_name["hunk_header_label"]
    if "split" in Path(filename).stem:
        assert by_name["diff_mode_active"]["text"] == "Split"
        assert "sbs_cell" in by_name
    assert hunk["font_size"]["value"] == 14, hunk
    assert len(hunk["text_spans"]) == 2, hunk
    if "narrow" not in Path(filename).stem:
        assert "context" in by_name["file_context_footer"]["text"].lower()
        assert "End of commit" in by_name["diff_reading_end"]["text"]
    if "viewed" in Path(filename).stem:
        assert "1 of 1" in by_name["diff_reading_end"]["text"]
        assert "viewed_check" in by_name
    print(f"PASS {filename}: separated path/stats, state checkbox, usable actions, quiet context and end marker")
