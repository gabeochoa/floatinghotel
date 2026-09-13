import json
import sys
from pathlib import Path


for filename in sys.argv[1:]:
    snapshot = json.loads(Path(filename).read_text())
    scale = snapshot["ui_scale"]
    nodes = {node["name"]: node for node in snapshot["nodes"] if node.get("name") and node["rendered"]}
    for name in ("commit_author", "commit_relative_date", "commit_sticky_revision", "commit_review_context"):
        assert name in nodes, (filename, name)
    title = nodes["commit_detail_subject"]
    assert title["rect"]["height"] / scale <= 72.1, title
    heading = nodes["commit_heading"]["rect"]
    scroll = nodes["commit_detail_scroll"]["rect"]
    assert heading["y"] + heading["height"] <= scroll["y"] + 0.1
    assert scroll["height"] / scale >= 100, "Header leaves too little room for code"
    if "commit_message_preview" in nodes:
        assert nodes["commit_message_preview"]["text_overflow"] == "Wrap"
    print(f"PASS {filename}: bounded title and description, separated metadata, usable code viewport")
