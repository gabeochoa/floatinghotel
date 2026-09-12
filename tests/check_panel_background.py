import json
import sys
from pathlib import Path

from PIL import Image


for filename in sys.argv[1:]:
    path = Path(filename)
    snapshot = json.loads(path.with_suffix(".json").read_text())
    panel = next(node["rect"] for node in snapshot["nodes"] if node.get("name") == "main_content")
    scale = snapshot["ui_scale"]
    image = Image.open(path).convert("RGB")
    for x in (panel["x"] - 4 * scale, panel["x"] + panel["width"] + 4 * scale):
        for fraction in (0.2, 0.5, 0.9):
            point = (int(x), int(panel["y"] + panel["height"] * fraction))
            actual = image.getpixel(point)
            assert actual == (21, 23, 27), (path, point, "Panel gutter exposes a different background", actual)
    print(f"PASS {path}: panel gutters match the window background")
