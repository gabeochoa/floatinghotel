import json
import pathlib
import sys


directory = pathlib.Path(sys.argv[1])
for name in ("toast_duplicate", "toast_hovered", "toast_stack", "toast_persistent", "toast_zoom_small"):
    snapshot = json.loads((directory / f"{name}.json").read_text())
    scale = snapshot["ui_scale"]
    viewport = snapshot["viewport"]
    nodes = [node for node in snapshot["nodes"] if node["rendered"]]
    cards = [node for node in nodes if node.get("name") == "toast_card"]
    assert 1 <= len(cards) <= 3, (name, len(cards))
    rects = sorted((node["rect"] for node in cards), key=lambda rect: rect["y"])
    for rect in rects:
        assert rect["x"] >= 0 and rect["y"] >= 0
        assert rect["x"] + rect["width"] <= viewport["width"]
        assert rect["y"] + rect["height"] <= viewport["height"] - 26 * scale
    for first, second in zip(rects, rects[1:]):
        assert second["y"] - first["y"] - first["height"] >= 7.9 * scale
    for node in cards:
        assert all(abs(value / scale - 12) < 0.1 for value in node["padding"].values())
    close_buttons = [node for node in nodes if node.get("name") == "toast_dismiss"]
    assert len(close_buttons) == len(cards)
    assert all(abs(node["rect"]["height"] / scale - 28) < 0.1 for node in close_buttons)
    print(f"PASS {name}: {len(cards)} padded, dismissible cards fit above the footer")
