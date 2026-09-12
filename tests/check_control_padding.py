import json
import pathlib
import sys


checked = 0
for filename in sys.argv[1:]:
    snapshot = json.loads(pathlib.Path(filename).read_text())
    nodes = [node for node in snapshot["nodes"] if node["rendered"]]
    for node in nodes:
        name = node.get("name", "")
        if name in ("tab_add", "tab_close", "zoom_in", "zoom_out") or name.startswith(("menu_header_", "menu_item_", "fold_file:", "jump_to_diff:")):
            padding = node["padding"]
            assert padding["top"] + padding["bottom"] <= node["rect"]["height"], (filename, name, padding)
            assert padding["left"] + padding["right"] <= node["rect"]["width"], (filename, name, padding)
            checked += 1
    subject = [node for node in nodes if node.get("name") == "commit_detail_subject"]
    metadata = [node for node in nodes if node.get("name") == "commit_meta_compact"]
    if subject and metadata:
        assert abs(subject[0]["rect"]["x"] - metadata[0]["rect"]["x"]) < 0.1, filename
assert checked, "No controls checked"
print(f"PASS {checked} control boxes have usable content space; commit headings align")
