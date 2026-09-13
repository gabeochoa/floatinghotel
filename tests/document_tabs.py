import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CHECKPOINTS = {
    "tabs_all": (6, "full_file_revision", "src/app.cpp @ working tree"),
    "tabs_first_review": (2, "commit_detail_subject", "Add contributing guidelines"),
    "tabs_second_review": (3, "commit_detail_subject", "Add unit tests for utils"),
    "tabs_first_source": (4, "full_file_revision", "README.md @ working tree"),
    "tabs_second_source": (5, "full_file_revision", "CONTRIBUTING.md @ working tree"),
    "tabs_third_source": (6, "full_file_revision", "src/app.cpp @ working tree"),
}


def check(directory):
    identity = None
    for label, (active, heading, text) in CHECKPOINTS.items():
        workspace = json.loads((directory / (label + ".workspace.json")).read_text())
        assert workspace["active"] == active, (label, workspace)
        assert workspace["inactive_payloads_empty"]
        assert len(workspace["tabs"]) == 6
        if identity is None:
            identity = workspace["tabs"]
        assert workspace["tabs"] == identity, label
        layout = json.loads((directory / (label + ".json")).read_text())
        nodes = [n for n in layout["nodes"] if n["rendered"] and not n["hidden"]]
        tabs = sorted([n for n in nodes if n.get("name", "").startswith("content_document_")],
                      key=lambda n: n["rect"]["x"])
        assert 1 <= len(tabs) <= 6, label
        activeTab = next(n for n in tabs if n.get("name") == f"content_document_{active}")
        assert activeTab["visible_rect"]["width"] >= activeTab["rect"]["width"] - .2, (label, "Active tab clipped")
        for tab in tabs:
            assert tab["visible_rect"]["width"] <= tab["rect"]["width"] + .2, (label, tab["name"])
            assert tab["visible_rect"]["height"] >= tab["rect"]["height"] - .2, (label, tab["name"])
        for left, right in zip(tabs, tabs[1:]):
            assert left["rect"]["x"] + left["rect"]["width"] <= right["rect"]["x"] + .2, label
        assert any(n.get("name") == heading and n.get("text") == text and
                   n["visible_rect"]["width"] > 100 and n["visible_rect"]["height"] > 0 for n in nodes), label
        assert (directory / (label + ".png")).read_bytes().startswith(b"\x89PNG\r\n\x1a\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = ROOT / "output/floatinghotel.exe"
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    source = (ROOT / "tests/e2e_scripts/reading_document_tabs.e2e").read_text()
    for zoom, increments in ((100, 0), (140, 4), (200, 10)):
        directory = output / str(zoom)
        directory.mkdir()
        setup = 'native_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * increments
        script = directory / "journey.e2e"
        script.write_text(source.replace("wait_for_refresh\n", "wait_for_refresh\n" + setup, 1))
        with (directory / "run.log").open("w") as log:
            result = subprocess.run([str(binary), "--test-mode", "--headless", f"--test-script={script}",
                                     f"--screenshot-dir={directory}", "--e2e-timeout=90"], cwd=ROOT,
                                    env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log,
                                    stderr=subprocess.STDOUT, timeout=120)
        assert result.returncode == 0, directory
        assert not any(token in (directory / "run.log").read_text() for token in ("E2E ERROR", "[TIMEOUT]", "(FAIL)")), directory
        check(directory)
        print(f"PASS {zoom}%: six independent tabs, six destinations, geometry and payload checks", flush=True)
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
    (output / "result.json").write_text(json.dumps(dict(binary_sha256=digest, zooms=[100, 140, 200], passed=True), indent=2) + "\n")


if __name__ == "__main__":
    main()
