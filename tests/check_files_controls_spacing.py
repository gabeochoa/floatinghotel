import json
import pathlib
import sys


for filename in sys.argv[1:]:
    snapshot = json.loads(pathlib.Path(filename).read_text())
    scale = snapshot["ui_scale"]
    nodes = {node["id"]: node for node in snapshot["nodes"] if node["rendered"]}

    def named(label):
        return [node for node in nodes.values() if node.get("name") == label]

    body = named("files_controls_body")[0]
    children = [nodes[key] for key in body["children"] if key in nodes]
    children.sort(key=lambda node: node["rect"]["y"])
    for first, second in zip(children, children[1:]):
        assert second["rect"]["y"] >= first["rect"]["y"] + first["rect"]["height"] - 0.1
    for child in children:
        for key in child["children"]:
            if key not in nodes or nodes[key]["absolute"]:
                continue
            rect = nodes[key]["rect"]
            assert rect["height"] <= child["rect"]["height"] + 0.1, (filename, child.get("name"), nodes[key].get("name"))
    assert abs(named("review_progress")[0]["rect"]["height"] / scale - 60) < 0.1
    viewport = named("files_controls_scroll")[0]
    log = named("sidebar_log")[0]
    assert viewport["rect"]["y"] + viewport["rect"]["height"] <= log["rect"]["y"]
    print(f"PASS {filename}: controls retain their heights and scroll above commit history")
