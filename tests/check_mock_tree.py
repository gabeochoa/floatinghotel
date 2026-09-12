import json
import sys
from collections import Counter
from pathlib import Path

from PIL import Image


for filename in sys.argv[1:]:
    snapshot = json.loads(Path(filename).read_text())
    scale = snapshot["ui_scale"]
    nodes = snapshot["nodes"]

    def named(name):
        return [node for node in nodes if node.get("name") == name and node["rendered"]]

    assert named("tree_review_progress"), "Missing viewed count"
    assert named("history_branch"), "Missing separate branch label"
    assert named("tree_search_icon"), "Missing search marker"
    files = named("commit_changed_file")
    assert files, "Fixture must display a changed file"
    assert named("tree_additions") and named("tree_deletions"), "Mixed changes must show both counts"
    assert named("tree_file_type"), "Missing file type marker"
    for row in files:
        assert abs(row["rect"]["height"] / scale - 28) < 0.1, row
        assert abs(row["margin"]["left"] / scale - 8) < 0.1, row
        assert row["visible_rect"]["width"] > 0, row
    for node in named("tree_directory_name"):
        assert not node["text"].startswith(("v ", "> ", " ")), node
    image = Image.open(Path(filename).with_suffix(".png")).convert("RGB")
    for node in named("tree_disclosure"):
        rect = node["visible_rect"]
        if rect["width"] <= 0 or rect["height"] <= 0:
            continue
        crop = image.crop((int(rect["x"]), int(rect["y"]),
                           int(rect["x"] + rect["width"]), int(rect["y"] + rect["height"])))
        colors = Counter(crop.getpixel((x, y)) for y in range(crop.height) for x in range(crop.width))
        assert crop.width * crop.height - colors.most_common(1)[0][1] >= 4, "Folder arrow is invisible"
    print(f"PASS {filename}: inset rows, real indentation, type markers, mixed counts, review progress")

collapsed = Path(sys.argv[1]).parent / "tree_collapsed.json"
if collapsed.exists():
    nodes = json.loads(collapsed.read_text())["nodes"]
    assert not any(node.get("name") == "commit_changed_file" and node["rendered"] for node in nodes)
    print("PASS folder collapse hides descendant rows")
