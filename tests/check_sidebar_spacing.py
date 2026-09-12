import json
import pathlib
import sys


for filename in sys.argv[1:]:
    snapshot = json.loads(pathlib.Path(filename).read_text())
    scale = snapshot["ui_scale"]
    nodes = {node["id"]: node for node in snapshot["nodes"]}

    def named(name):
        return [node for node in nodes.values() if node.get("name") == name and node["rendered"]]

    rows = named("commit_row")
    ages = named("commit_age")
    assert rows and ages, filename
    edges = [(age["rect"]["x"] + age["rect"]["width"]) / scale for age in ages]
    assert max(edges) - min(edges) < 0.1, (filename, "age edges", edges)
    for row in rows:
        assert row["margin"]["left"] / scale <= 4.01, row
        assert row["padding"]["left"] / scale <= 4.01, row
        assert abs(row["rect"]["height"] / scale - 24) < 0.1, row
    for age in ages:
        assert age["text_alignment"] == "Right", age
        siblings = nodes[age["parent"]]["children"]
        assert siblings[-1] == age["id"], ("Age must follow badges", siblings)
    for subject in named("commit_subject"):
        parent = nodes[subject["parent"]]
        siblings = [nodes[key] for key in parent["children"] if key in nodes and key != subject["id"]]
        assert siblings
        gap = min(node["rect"]["x"] for node in siblings) - subject["rect"]["x"] - subject["rect"]["width"]
        assert gap / scale >= 7.9, gap
    dividers = named("sidebar_divider")
    if dividers:
        sidebar = named("sidebar_bg")[0]["rect"]
        for divider in dividers:
            assert abs(divider["rect"]["y"] - sidebar["y"]) < 0.1
            assert abs(divider["rect"]["height"] - sidebar["height"]) < 0.1
    for empty in named("commit_files_empty"):
        parent = nodes[empty["parent"]]
        assert abs(empty["rect"]["y"] - parent["rect"]["y"]) < 0.1
        assert empty["rect"]["height"] / scale <= 28.1
    print(f"PASS {filename}: compact rows, aligned ages, full-height divider, top-aligned empty text")
