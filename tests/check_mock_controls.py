import json
import sys
from pathlib import Path


for filename in sys.argv[1:]:
    snapshot = json.loads(Path(filename).read_text())
    scale = snapshot["ui_scale"]
    nodes = snapshot["nodes"]

    def named(name):
        return [node for node in nodes if node.get("name") == name and node["rendered"]]

    for name in ("sidebar_navigation_segments", "navigation_icon", "commit_tab_icon",
                 "content_tab_indicator", "review_segments", "diff_additions", "diff_deletions",
                 "feedback_icon", "finish_review_icon"):
        assert named(name), (filename, name)
    assert "1 file changed" in named("diff_stats_label")[0]["text"]
    for node in named("content_tab_indicator"):
        assert abs(node["rect"]["height"] / scale - 2) < 0.1, node
    actions = named("diff_mode_toggle")[0]["rect"]
    by_id = {node["id"]: node for node in nodes}
    for key in named("diff_mode_toggle")[0]["children"]:
        rect = by_id[key]["rect"]
        assert rect["x"] >= actions["x"] - 0.1
        assert rect["x"] + rect["width"] <= actions["x"] + actions["width"] + 0.1, by_id[key]
    print(f"PASS {filename}: segmented controls, native markers, colored totals, inset tab indicator, fitted actions")
