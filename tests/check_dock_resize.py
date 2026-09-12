import json
import sys
from pathlib import Path


directory = Path(sys.argv[1])
for name in ("dock_352", "dock_480", "dock_300", "dock_zoom", "dock_return"):
    snapshot = json.loads((directory / f"{name}.json").read_text())
    nodes = {node["name"]: node for node in snapshot["nodes"] if node.get("name") and node["rendered"]}
    viewport = snapshot["viewport"]
    for label in ("sidebar_bg", "sidebar_log", "sidebar_worktree_status"):
        rect = nodes[label]["rect"]
        assert abs(rect["x"]) < 0.1, (name, label, rect)
        assert abs(rect["width"] - viewport["width"]) < 1, (name, label, rect, viewport)
    assert "commit_detail_subject" not in nodes
    print(f"PASS {name}: dock sidebar fills all {viewport['width']} pixels")

snapshot = json.loads((directory / "expanded.json").read_text())
nodes = {node["name"]: node for node in snapshot["nodes"] if node.get("name") and node["rendered"]}
assert abs(nodes["sidebar_bg"]["rect"]["width"] / snapshot["ui_scale"] - 280) < 0.1
assert "commit_detail_subject" in nodes
print("PASS expanded: review content remains usable with its configured sidebar width")
