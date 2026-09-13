import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--zooms", nargs="+", type=int, default=[100, 140, 200])
parser.add_argument("--scopes", nargs="+", default=["working", "index", "commit"])
parser.add_argument("--snapshots", action="store_true")
parser.add_argument("--syntax", action="store_true")
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
repo.mkdir()
def git(*command):
    return subprocess.check_output(["git", "-C", str(repo), *command], text=True).strip()
for command in [("init", "-q", "-b", "main"), ("config", "user.name", "Continuous source"),
                ("config", "user.email", "pages@example.invalid"), ("config", "commit.gpgsign", "false")]:
    git(*command)
versions = {name: "".join(f"{name if i % 1024 == 1 else 'source'}_{i:05} éλ\tvalue\r\n" for i in range(1, 26000)) + f"{name}_26000 final"
            for name in ["original", "staged", "working"]}
filename = "rows.cpp" if args.syntax else "rows.txt"
if args.syntax:
    versions = {name: "/*\n" + text.split("\n", 1)[1].rsplit("\n", 1)[0] + "\n*/" for name, text in versions.items()}
(repo / filename).write_bytes(versions["original"].encode())
(repo / "other.txt").write_text("Other document\n")
(repo / "empty.txt").write_text("")
git("add", ".")
git("commit", "-qm", "Continuous source base")
commit = git("rev-parse", "HEAD")
(repo / filename).write_bytes(versions["staged"].encode())
git("add", filename)
(repo / filename).write_bytes(versions["working"].encode())
before = git("diff"), git("diff", "--cached")
wrapper = out / "bin"
wrapper.mkdir()
real_git = shutil.which("git")
(wrapper / "git").write_text("#!/usr/bin/env python3\nimport os, sys, time\n"
    + "if 'cat-file' in sys.argv and 'blob' in sys.argv: time.sleep(0.35)\n"
    + f"os.execv({real_git!r}, [{real_git!r}, *sys.argv[1:]])\n")
(wrapper / "git").chmod(0o755)
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
timing_failures = []
def capture(label, count, settle=True):
    wait = "wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\n" if settle else "wait_frames 3\n"
    return wait + f"workspace_checkpoint {count} {label}\nscreenshot {label}\n"
def open_file(path):
    return f'key CMD+P\nwait_for_refresh\nscreenshot picker_ready\ntype "{path}"\nkey ENTER\n'
def visible(path):
    snapshot = json.loads(path.read_text())
    viewport = next(n["visible_rect"] for n in snapshot["nodes"] if n.get("name") == "diff_scroll" and n["rendered"])
    rows = [r for r in snapshot["reading_rows"] if r["rect"]["y"] + r["rect"]["height"] > viewport["y"]
            and r["rect"]["y"] < viewport["y"] + viewport["height"]]
    return snapshot, viewport, rows
for zoom in args.zooms:
    for scope in args.scopes:
        directory = out / f"{zoom}-{scope}"
        directory.mkdir()
        settings = directory / "settings"
        settings.mkdir()
        count = 2 if scope == "working" else 3
        script = "resize 1700 1100\nwait_for_refresh\nnative_menu_action \"Reset Zoom\"\n"
        script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
        script += "screenshot zoom_ready\nclick_ui review_unstaged_changes\n" + capture("review", 1)
        if scope == "index": script += "click_ui review_staged_changes\n" + capture("scope", 2)
        if scope == "commit": script += 'click_text "Continuous source base"\n' + capture("scope", 2)
        script += open_file(filename) + capture("initial", count)
        for index in range(1, 6):
            script += "hover_ui diff_scroll\nscroll_wheel 0 -100000\n"
            script += capture(f"pending_{index}", count, False) + capture(f"forward_{index}", count)
        script += "bench_frames 120\nexpect_p99_below 20\nkey CMD+EQUAL\nresize 1400 900\n" + capture("resized", count)
        script += "bench_frames 120\nexpect_p99_below 20\n"
        for index in range(1, 5):
            script += "hover_ui diff_scroll\nscroll_wheel 0 100000\n" + capture(f"backward_{index}", count)
        script += open_file("other.txt") + capture("other", count + 1)
        badge = "" if scope == "working" else " · " + ("Index" if scope == "index" else commit[:7])
        script += f"click_ui open_tabs_menu\nscreenshot tabs_ready\nclick_ui context_menu_item_{filename}{badge}\n" + capture("returned", count + 1)
        script += "hover_ui diff_scroll\nscroll_wheel 0 -100000\nwait_frames 3\nkey CMD+W\n" + capture("closed_loading", count)
        script += "bench_frames 120\nexpect_p99_below 20\n"
        path = directory / "journey.e2e"
        path.write_text(script)
        with (directory / "journey.log").open("w") as log:
            result = subprocess.run([str(binary), str(repo), "--test-mode", *(["--headless"] if args.snapshots else []),
                f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=240"], cwd=ROOT,
                env=dict(os.environ, FH_NATIVE_MENUS="1", FH_TEST_NATIVE_HIDDEN="1", FH_TEST_SETTINGS_DIR=str(settings),
                         PATH=str(wrapper) + os.pathsep + os.environ["PATH"]), stdout=log, stderr=subprocess.STDOUT, timeout=270)
        log_text = (directory / "journey.log").read_text()
        errors = [line for line in log_text.splitlines() if "[E2E ERROR]" in line or "[TIMEOUT]" in line]
        if result.returncode:
            assert result.returncode == 1 and errors and all("[E2E ERROR] expect_p99_below" in line for line in errors), directory / "journey.log"
            timing_failures.append(dict(case=directory.name, errors=errors))
        if not args.snapshots: assert "Native test window hidden=1 key=0" in log_text
        expected = versions[{"working": "working", "index": "staged", "commit": "original"}[scope]].splitlines(keepends=True)
        states = {}
        for label in ["initial", *[f"forward_{i}" for i in range(1, 6)], "resized", *[f"backward_{i}" for i in range(1, 5)], "returned"]:
            state = json.loads((directory / f"{label}.workspace.json").read_text())
            window = state["source_pages"]
            states[label] = window
            assert window["bounded"] and not window["error"], (label, window)
            assert 1 <= len(window["pages"]) <= 3, (label, window)
            assert {p["identity"] for p in window["pages"]} == {states["initial"]["pages"][0]["identity"]}
            tab = next(t for t in state["tabs"] if t["id"] == state["active"])
            assert tab["revision"] == {"working": "", "index": "INDEX", "commit": commit}[scope]
            assert window["raw_bytes"] <= 3 * 256 * 1024
            snapshot, viewport, rows = visible(directory / f"{label}.json")
            assert rows, label
            assert not any(n.get("name") == "file_page_next" and n["rendered"] for n in snapshot["nodes"])
            for row in rows:
                text = expected[row["line"] - 1].removesuffix("\n")
                assert text[row["column"] - 1:].startswith(row["text"]), (label, row, text)
                if args.syntax:
                    node = next(n for n in snapshot["nodes"] if n["id"] == row["id"] and n["rendered"])
                    spans = [span for span in node["text_spans"][1:] if span["text"].strip()]
                    assert spans and all(span["color"] == [117, 129, 142, 255] for span in spans), (label, row, spans)
            positions = [(r["line"], r["column"]) for r in rows]
            assert len(set(positions)) == len(positions), (label, positions)
        assert len(states["initial"]["pages"]) == 1
        assert len(states["forward_1"]["pages"]) == 2
        assert len(states["forward_2"]["pages"]) == 3
        assert states["forward_3"]["pages"][0]["begin"] > 0
        assert states["backward_4"]["pages"][0]["begin"] < states["forward_5"]["pages"][0]["begin"]
        pending_checks = 0
        for index in range(1, 6):
            pending = json.loads((directory / f"pending_{index}.workspace.json").read_text())
            if pending["source_pages"]["loading"]:
                pending_checks += 1
                _, before_view, before_rows = visible(directory / f"pending_{index}.json")
                _, after_view, after_rows = visible(directory / f"forward_{index}.json")
                anchor = next(r for r in before_rows if r["rect"]["y"] >= before_view["y"])
                matches = [r for r in after_rows if (r["line"], r["column"]) == (anchor["line"], anchor["column"])]
                assert matches and abs(matches[0]["rect"]["y"] - after_view["y"] - (anchor["rect"]["y"] - before_view["y"])) < 2, (index, anchor, matches)
        if scope != "working": assert pending_checks > 0, (zoom, scope)
        for before_label, after_label in [("forward_5", "resized"), ("backward_4", "returned")]:
            previous = json.loads((directory / f"{before_label}.workspace.json").read_text())
            anchor = previous["history"][previous["history_index"]]["anchor"]
            _, viewport, rows = visible(directory / f"{after_label}.json")
            matches = [r for r in rows if r["line"] == anchor["line"] and r["column"] == anchor["column"]]
            expected_y = viewport["y"] + anchor["fraction"] * viewport["height"]
            assert matches and abs(matches[0]["rect"]["y"] - expected_y) < 2, (before_label, after_label, anchor, matches)
        closed = json.loads((directory / "closed_loading.workspace.json").read_text())
        tab = next(t for t in closed["tabs"] if t["id"] == closed["active"])
        assert tab["path"] == "other.txt", tab
        assert git("diff") == before[0] and git("diff", "--cached") == before[1]
        print(f"PASS behavior {zoom}% {scope}: adjacent pages, eviction, reverse scroll, version identity, exact visible text, resize and cancellation", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=not timing_failures, binary_sha256=digest, timing_failures=timing_failures), indent=2) + "\n")
assert not timing_failures, timing_failures
