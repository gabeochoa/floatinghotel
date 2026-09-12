import json
import sys
from pathlib import Path


snapshot = json.loads(Path(sys.argv[1]).read_text())
nodes = [node for node in snapshot["nodes"] if node["rendered"]]
headers = sorted((node["rect"] for node in nodes if node.get("name") == "file_header_row"), key=lambda rect: rect["y"])
assert len(headers) == 2, headers
gap = (headers[1]["y"] - headers[0]["y"] - headers[0]["height"]) / snapshot["ui_scale"]
assert abs(gap - 14) < 0.1, gap
assert not any(node.get("name") == "hunk_header_label" for node in nodes)
assert any(node.get("name") == "diff_reading_end" for node in nodes)
print(f"PASS {sys.argv[1]}: folded files retain fourteen-pixel separation and the end marker")
