import json
import math
import pathlib
import sys

from PIL import Image


directory = pathlib.Path(sys.argv[1])
screenshots = sorted(directory.glob("*.png"))
assert screenshots, f"No screenshots in {directory}"
for screenshot in screenshots:
    snapshot = json.loads(screenshot.with_suffix(".json").read_text())
    assert snapshot["schema_version"] == 1
    assert snapshot["units"] == "physical_pixels"
    viewport = snapshot["viewport"]
    with Image.open(screenshot) as image:
        assert image.size == (viewport["width"], viewport["height"])
    nodes = snapshot["nodes"]
    assert len(nodes) > 10
    assert len({node["id"] for node in nodes}) == len(nodes)
    rendered = [node for node in nodes if node["rendered"]]
    assert len(rendered) > 10
    assert any(node.get("name") == "sidebar_bg" for node in rendered)
    for node in nodes:
        for field in ("rect", "visible_rect", "padding", "margin"):
            assert all(math.isfinite(value) for value in node[field].values()), node
        assert node["rect"]["width"] >= 0 and node["rect"]["height"] >= 0
        assert node["visible_rect"]["width"] >= 0 and node["visible_rect"]["height"] >= 0
        assert math.isfinite(node["gap"])
    print(f"PASS {screenshot.name}: {len(rendered)} rendered nodes with measured spacing")

standalone = json.loads((directory / "spacing_dump.json").read_text())
paired = json.loads((directory / "spacing_review.json").read_text())
for name in ("sidebar_bg", "log_header", "commit_row"):
    actual = [node["rect"] for node in standalone["nodes"] if node.get("name") == name and node["rendered"]]
    expected = [node["rect"] for node in paired["nodes"] if node.get("name") == name and node["rendered"]]
    assert actual and actual == expected, name
print("PASS standalone dump matches screenshot geometry")
