import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--from-stage", choices=["hover", "wrap", "selection", "window", "large-source", "divider", "header", "tabs", "navigation", "priority", "native-menu", "default-font", "font-controls", "full-message", "metrics-regression", "native-resize", "reading"])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
results = []
started = args.from_stage is None
env = dict(os.environ)
env.pop("FH_NATIVE_MENUS", None)
env.pop("FH_TEST_SETTINGS_DIR", None)


def run(name, command, native=False):
    global started
    if not started and name != args.from_stage:
        return False
    started = True
    start = time.monotonic()
    print("RUN " + name, flush=True)
    with (out / (name + ".log")).open("w") as log:
        result = subprocess.run(command, cwd=ROOT, env=dict(env, FH_NATIVE_MENUS="1") if native else env,
            stdout=log, stderr=subprocess.STDOUT, timeout=1200)
    results.append(dict(name=name, exit_code=result.returncode, seconds=time.monotonic() - start))
    (out / "results.json").write_text(json.dumps(dict(binary_sha256=digest, from_stage=args.from_stage, results=results), indent=2) + "\n")
    assert result.returncode == 0, (name, out / (name + ".log"))
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest, "Binary changed during replay"
    print("PASS " + name, flush=True)
    return True


for name, script in (("hover", "zoom_hover"), ("wrap", "code_wrap"), ("selection", "text_highlight"),
                     ("window", "window_restore"), ("large-source", "large_source_wrap"), ("divider", "zoom_drag"), ("header", "compact_commit"),
                     ("tabs", "document_tabs"), ("navigation", "navigation_regressions")):
    run(name, [sys.executable, f"tests/{script}.py", "--output", str(out / name)])

for name, path, native in (("priority", "tests/e2e_scripts/reading_priority_polish.e2e", True),
                           ("native-menu", "tests/review_focus/native_menu.e2e", True),
                           ("default-font", "tests/review_focus/default_font.e2e", False),
                           ("font-controls", "tests/e2e_scripts/improvement_48_code_font.e2e", False)):
    directory = out / name
    directory.mkdir()
    if not run(name, [str(binary), "--test-mode", "--headless", f"--test-script={path}",
        f"--screenshot-dir={directory}", "--e2e-timeout=90"], native):
        continue
    if name == "priority":
        run("priority-geometry", [sys.executable, "tests/check_priority_polish.py", str(directory)])
        staged = subprocess.run(["git", "-C", "/tmp/floatinghotel_test_repo", "diff", "--cached", "--name-only"],
            check=True, text=True, capture_output=True).stdout.strip()
        assert staged == ".gitignore", staged
        (out / "index-after-feedback.txt").write_text(staged + "\n")

run("full-message", ["bash", "tests/check_commit_message.sh", str(out / "full-message")])
run("metrics-regression", ["bash", "tests/review_50/item_05.sh"])
run("native-resize", ["bash", "tests/check_native_window_resize.sh"])
run("reading", [sys.executable, "tests/reading_journey.py", "--runs", "3", "--zooms", "100", "140", "200",
    "--output", str(out / "reading")])
print("Selected priority replay checks passed", flush=True)
