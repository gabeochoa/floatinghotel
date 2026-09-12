import json
import pathlib
import sys


directory = pathlib.Path(sys.argv[1])
for name in ("basket_100", "basket_140", "basket_small", "basket_edit", "basket_compact"):
    snapshot = json.loads((directory / f"{name}.json").read_text())
    scale = snapshot["ui_scale"]
    nodes = {node["id"]: node for node in snapshot["nodes"] if node["rendered"]}

    def named(label):
        return [node for node in nodes.values() if node.get("name") == label]

    panel = named("feedback_basket")[0]
    assert all(abs(value / scale - 12) < 0.1 for value in panel["padding"].values()), panel
    assert panel["rect"]["x"] >= 0
    assert panel["rect"]["x"] + panel["rect"]["width"] <= snapshot["viewport"]["width"] + 0.1
    assert named("basket_close") and not named("basket_copy_btn")
    for label in ("basket_close", "basket_item_loc", "basket_item_edit", "basket_item_resolve", "basket_item_remove"):
        for node in named(label):
            assert node["rect"]["height"] / scale >= 27.9, (label, node["rect"])
    for item in named("basket_item"):
        children = [nodes[key] for key in item["children"] if key in nodes]
        children.sort(key=lambda node: node["rect"]["y"])
        for first, second in zip(children, children[1:]):
            gap = second["rect"]["y"] - first["rect"]["y"] - first["rect"]["height"]
            assert gap / scale >= 7.9, (name, first.get("name"), second.get("name"), gap)
    print(f"PASS {name}: padded feedback, separate actions, close control, one export action")
