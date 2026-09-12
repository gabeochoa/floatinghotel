import json
from pathlib import Path
import sys


root = Path(sys.argv[1])
expectations = {
    "boundary_commit": ("commit_detail_subject", "Add contributing guidelines"),
    "boundary_first_source": ("full_file_revision", "CONTRIBUTING.md @"),
    "boundary_second_source": ("full_file_revision", "README.md @ working tree"),
    "boundary_retained_review": ("commit_detail_subject", "Add contributing guidelines"),
    "boundary_retained_source": ("full_file_revision", "README.md @ working tree"),
    "boundary_closed_source": ("commit_detail_subject", "Add contributing guidelines"),
    "boundary_revisited_source": ("full_file_revision", "README.md @ working tree"),
    "boundary_comparison": (None, "Resolved revisions:"),
    "boundary_comparison_file": (None, "Resolved revisions:"),
    "boundary_comparison_source": ("full_file_revision", "CONTRIBUTING.md @"),
    "boundary_comparison_return": (None, "Resolved revisions:"),
    "boundary_comparison_reloaded": (None, "Resolved revisions:"),
}
for checkpoint, (name, text) in expectations.items():
    layout = json.loads((root / f"{checkpoint}.json").read_text())
    nodes = [n for n in layout["nodes"] if n["rendered"] and not n["hidden"]]
    main = next(n for n in nodes if n.get("name") == "main_content")
    matches = [n for n in nodes if (name is None or n.get("name") == name)
               and n.get("text", "").startswith(text)]
    assert matches, (checkpoint, name, text)
    assert any(n["visible_rect"]["width"] > 100
               and n["visible_rect"]["height"] >= n["rect"]["height"] - .1
               and n["visible_rect"]["x"] >= main["visible_rect"]["x"] for n in matches), checkpoint
    if checkpoint == "boundary_comparison_reloaded":
        assert any(n.get("name") == "file_header_label" and n.get("text") == "CONTRIBUTING.md"
                   and n["visible_rect"]["width"] > 100 and n["visible_rect"]["height"] > 0 for n in nodes), checkpoint
    tabs = next(n for n in nodes if n.get("name") == "content_tabs")
    assert tabs["visible_rect"]["width"] == tabs["rect"]["width"], checkpoint
    assert tabs["visible_rect"]["height"] == tabs["rect"]["height"], checkpoint
    assert (root / f"{checkpoint}.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), checkpoint
print(f"Verified reader headings, tab geometry, and screenshots for {len(expectations)} checkpoints")
