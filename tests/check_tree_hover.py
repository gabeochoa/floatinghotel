import json
import sys
from pathlib import Path

from PIL import Image


directory = Path(sys.argv[1])


def check_hover(neutral_name, hovered_name, row_name):
    snapshot = json.loads((directory / f"{neutral_name}.json").read_text())
    rows = [node for node in snapshot["nodes"] if node.get("name") == row_name and node["rendered"]]
    assert rows, (neutral_name, row_name, "missing row")
    neutral = Image.open(directory / f"{neutral_name}.png").convert("RGB")
    hovered = Image.open(directory / f"{hovered_name}.png").convert("RGB")
    for index, row in enumerate(rows):
        rect = row["visible_rect"]
        if rect["height"] < 20:
            continue
        y = int(rect["y"] + rect["height"] / 2)
        for x in (int(rect["x"] + 5), int(rect["x"] + rect["width"] - 5)):
            changed = neutral.getpixel((x, y)) != hovered.getpixel((x, y))
            assert changed == (index == 0), (hovered_name, index, x, y, "wrong row hover")
    print(f"PASS {hovered_name}: full first row highlights; neighboring file rows stay unchanged")


for name in ("hover_label", "hover_icon", "hover_count"):
    check_hover("neutral", name, "commit_changed_file")
check_hover("neutral", "hover_folder", "commit_directory:src/")
check_hover("working_neutral", "working_hover", "file_row")
