import json
import math
import pathlib
import sys


snapshot = json.loads(pathlib.Path(sys.argv[1]).read_text())
assert snapshot["nodes"]
assert any("\ufffd" in node.get("text", "") for node in snapshot["nodes"])
for node in snapshot["nodes"]:
    assert all(math.isfinite(value) for value in node["rect"].values())
print("PASS binary label bytes are safely replaced and UI geometry is preserved")
