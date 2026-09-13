import json
from pathlib import Path
import sys

checks = []
for root in map(Path, sys.argv[1:]):
    for path in sorted(root.glob("*/*.workspace.json")):
        if path.stem.removesuffix(".workspace") not in {"extent", "resized", "copied", "reopened", "drag_copied"}:
            continue
        workspace = json.loads(path.read_text())
        tab = next(t for t in workspace["tabs"] if t["id"] == workspace["active"])
        selection = tab["selection"]
        snapshot = json.loads(path.with_name(path.name.replace(".workspace", "")).read_text())
        viewport = next(n["visible_rect"] for n in snapshot["nodes"] if n.get("name") == "diff_scroll" and n["rendered"])
        rows = {row["id"]: row for row in snapshot["reading_rows"]}
        highlights = [n for n in snapshot["nodes"] if n.get("name") == "diff_sel_hl" and n["rendered"]
                      and n["visible_rect"]["width"] > 0 and n["visible_rect"]["height"] > 0]
        assert highlights, path
        for node in highlights:
            visible, rect = node["visible_rect"], node["rect"]
            row = rows[node["parent"]]
            assert visible["x"] >= viewport["x"] - 1 and visible["y"] >= viewport["y"] - 1, path
            assert visible["x"] + visible["width"] <= viewport["x"] + viewport["width"] + 1, path
            assert visible["y"] + visible["height"] <= viewport["y"] + viewport["height"] + 1, path
            assert rect["x"] >= row["content_x"] - 1, path
            assert rect["x"] + rect["width"] <= row["rect"]["x"] + row["rect"]["width"] + 1, path
            assert row["path"] == selection["path"], path
            assert min(selection["anchor_line"], selection["head_line"]) <= row["line"] <= max(selection["anchor_line"], selection["head_line"]), path
            if selection["side"] == "before":
                assert row["side"] != 2 and row["sign"] != "+", path
            else:
                assert row["side"] != 1 and row["sign"] != "-", path
        checks.append({"snapshot": str(path), "visible_highlights": len(highlights)})
assert checks
print(json.dumps({"passed": True, "checks": checks}, indent=2))
