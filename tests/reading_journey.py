import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def fixture(path):
    path.mkdir(parents=True)
    env = dict(os.environ, GIT_AUTHOR_DATE="2026-01-01T00:00:00Z",
               GIT_COMMITTER_DATE="2026-01-01T00:00:00Z")

    def git(*args):
        return run("git", "-C", str(path), *args, env=env, capture_output=True).stdout.strip()

    git("init", "-q", "-b", "main")
    git("config", "user.name", "Reading fixture")
    git("config", "user.email", "reading@example.invalid")
    git("config", "commit.gpgsign", "false")
    for name in ("alpha.cpp", "beta.cpp", "gamma.cpp"):
        (path / name).write_text("\n".join(f"int {name[:-4]}_{i} = {i};" for i in range(1, 181)) + "\n")
    git("add", ".")
    git("commit", "-qm", "Reading root")
    root = git("rev-parse", "HEAD")
    for name in ("alpha.cpp", "beta.cpp"):
        p = path / name
        p.write_text(p.read_text().replace("_12 = 12;", "_12 = 120;"))
    git("add", ".")
    git("commit", "-qm", "Reading change")
    head = git("rev-parse", "HEAD")
    (path / "beta.cpp").write_text((path / "beta.cpp").read_text().replace("_12 = 120;", "_12 = 121;"))
    return {"root": root, "head": head}


def script(head, zoom):
    setup = ["resize 1600 1000", "wait_for_refresh", 'native_menu_action "Reset Zoom"']
    setup += ['native_menu_action "Zoom In"'] * {100: 0, 140: 4, 200: 10}[zoom]
    setup += ["screenshot ready"]
    for temperature in ("cold", "warm"):
        if temperature == "warm":
            setup += ["click_ui sidebar_review", "screenshot review_sidebar", 'click_text "Reading root"',
                      "wait_for_refresh", "screenshot root"]

        def step(label, kind, path, revision, action):
            name = f"{temperature}_{label}"
            setup.extend([f"reading_probe {name} {kind} {path} {revision}", action,
                          "reading_checkpoint", f"screenshot {name}",
                          "validate blob_cache_bounded=true", "validate patch_cache_bounded=true"])

        step("commit", "review", "-", head, 'click_text "Reading change"')
        step("diff", "review", "alpha.cpp", head, "click_ui jump_to_diff:alpha.cpp")
        step("source", "source", "alpha.cpp", head, "click_ui open_full_file")
        setup += ["key CMD+P", f"screenshot {temperature}_picker", "click_ui file_picker_input",
                  "key CMD+A", "type beta.cpp", f"screenshot {temperature}_query"]
        step("second", "source", "beta.cpp", "-", "key ENTER")
        step("back", "source", "alpha.cpp", head, "key ALT+LEFT")
    setup += ["bench_frames 120", "expect_p99_below 20"]
    return "\n".join(setup) + "\n"


def check(directory, head, zoom):
    rows = []
    for temperature in ("cold", "warm"):
        for step, kind, path, revision in (("commit", "review", "", head),
                                         ("diff", "review", "alpha.cpp", head),
                                         ("source", "source", "alpha.cpp", head),
                                         ("second", "source", "beta.cpp", ""),
                                         ("back", "source", "alpha.cpp", head)):
            name = f"{temperature}_{step}"
            row = json.loads((directory / f"{name}.reading.json").read_text())
            assert (row["kind"], row["path"], row["revision"]) == (kind, path, revision), row
            assert 0 <= row["selection_ms"] <= row["ready_ms"]
            assert row["owned_content_bytes"] > 0
            assert row["blob_cache"]["bytes"] <= 32 * 1024 * 1024
            assert row["patch_cache"]["bytes"] <= 32 * 1024 * 1024
            layout = json.loads((directory / f"{name}.json").read_text())
            assert math.isclose(layout["ui_scale"], zoom / 100, abs_tol=0.01), layout["ui_scale"]
            nodes = [n for n in layout["nodes"] if n["rendered"] and not n["hidden"]]
            tabs = [n for n in nodes if n.get("name") == "content_tabs"]
            assert len(tabs) == 1 and tabs[0]["visible_rect"]["width"] > 100
            if kind == "review":
                assert row["selected_commit"] == revision
                assert any(n.get("name") == "commit_detail_subject" and n.get("text") == "Reading change" for n in nodes)
            if kind == "source":
                assert row["selected_commit"] == head, "Opening source changed the retained review"
                assert (row["source_path"], row["source_revision"]) == (path, revision)
                assert any(n.get("name") == "full_file_revision" and path in n.get("text", "") for n in nodes)
                assert any(n.get("text", "").endswith("int beta_12 = 121;" if path == "beta.cpp" else "int alpha_12 = 120;") for n in nodes)
                first_line = "int beta_1 = 1;" if path == "beta.cpp" else "int alpha_1 = 1;"
                assert any(n.get("text", "").endswith(first_line) and n["visible_rect"]["height"] >= n["rect"]["height"] - 0.1
                           and n["visible_rect"]["width"] > 100 for n in nodes)
            png = directory / f"{name}.png"
            assert png.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n" and png.stat().st_size > 10000
            row.update(zoom=zoom, temperature=temperature, step=step, evidence=str(directory))
            rows.append(row)
    assert rows[-1]["patch_cache"]["hits"] > 0
    assert rows[-1]["blob_cache"]["hits"] > 0
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "output/reading-navigation" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S-%fZ"))
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--zooms", type=int, nargs="+", default=[100, 140, 200], choices=[100, 140, 200])
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    repo = output / "fixture"
    commits = fixture(repo)
    binary = ROOT / "output/floatinghotel.exe"
    metadata = dict(commits=commits, platform=platform.platform(), machine=platform.machine(),
                    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    checkout=run("git", "rev-parse", "HEAD", cwd=ROOT, capture_output=True).stdout.strip(),
                    diff=run("git", "diff", "--stat", cwd=ROOT, capture_output=True).stdout,
                    timing="Synthetic input dispatch to CPU completion of the first matching rendered frame; excludes screenshot readback and OS presentation.",
                    temperature="Cold means first journey in a fresh process; warm repeats that journey. Back may hit caches in either cycle. Working-tree reads are not immutable cache hits.",
                    memory="Owned vector/string capacity estimates for working, staged, source and commit content; excludes allocator metadata, set nodes, workers, GPU and caches, which are separate.")
    metadata["status"] = run("git", "status", "--short", cwd=ROOT, capture_output=True).stdout
    tracked = run("git", "ls-files", "-co", "--exclude-standard", "-z", cwd=ROOT, capture_output=True).stdout.split("\0")
    metadata["source_sha256"] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                 for name in sorted(set(tracked)) if name and (ROOT / name).is_file() and
                                 (name.startswith(("src/", "tests/")) or name == "makefile")}
    (output / "source.patch").write_text(run("git", "diff", "--binary", cwd=ROOT, capture_output=True).stdout)
    for name in run("git", "ls-files", "--others", "--exclude-standard", "-z", cwd=ROOT, capture_output=True).stdout.split("\0"):
        if name and name.startswith(("src/", "tests/")):
            snapshot = output / "untracked-source" / name
            snapshot.parent.mkdir(parents=True, exist_ok=True)
            snapshot.write_bytes((ROOT / name).read_bytes())
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    rows = []
    for zoom in args.zooms:
        for iteration in range(args.runs):
            directory = output / f"zoom-{zoom}-run-{iteration + 1}"
            directory.mkdir()
            scenario = directory / "journey.e2e"
            scenario.write_text(script(commits["head"], zoom))
            with (directory / "run.log").open("w") as log:
                run(str(binary), str(repo), "--test-mode", "--headless", f"--test-script={scenario}",
                    f"--screenshot-dir={directory}", "--e2e-timeout=45", cwd=ROOT,
                    env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
            assert "E2E ERROR" not in (directory / "run.log").read_text()
            rows.extend(check(directory, commits["head"], zoom))
            print(f"PASS zoom={zoom} run={iteration + 1}", flush=True)
    (output / "samples.json").write_text(json.dumps(rows, indent=2) + "\n")
    summary = []
    for zoom in args.zooms:
        for temperature in ("cold", "warm"):
            for step in ("commit", "diff", "source", "second", "back"):
                group = [r for r in rows if (r["zoom"], r["temperature"], r["step"]) == (zoom, temperature, step)]
                result = dict(zoom=zoom, temperature=temperature, step=step, count=len(group))
                for key in ("selection_ms", "ready_ms", "owned_content_bytes"):
                    values = sorted(r[key] for r in group)
                    result[key] = dict(min=values[0], p95=values[math.ceil(len(values) * .95) - 1], max=values[-1])
                summary.append(result)
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"Evidence: {output}")


if __name__ == "__main__":
    main()
