import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ["improvement_21_file_picker", "improvement_22_repo_search",
           "improvement_24_all_files", "improvement_27_commit_search",
           "improvement_35_comment_jump", "improvement_44_navigation",
           "reading_navigation_boundary"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "output/navigation-regressions" /
                        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S-%fZ"))
    parser.add_argument("--scripts", nargs="+", choices=SCRIPTS, default=SCRIPTS)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = ROOT / "output/floatinghotel.exe"
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    results = []
    for name in args.scripts:
        directory = output / name
        directory.mkdir()
        env = dict(os.environ)
        env.pop("FH_NATIVE_MENUS", None)
        if name == "reading_navigation_boundary":
            env["FH_NATIVE_MENUS"] = "1"
        with (directory / "run.log").open("w") as log:
            result = subprocess.run([str(binary), "--test-mode", "--headless",
                                     f"--test-script={ROOT / 'tests/e2e_scripts' / (name + '.e2e')}",
                                     f"--screenshot-dir={directory}", "--e2e-timeout=90"],
                                    cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        errors = [line for line in (directory / "run.log").read_text().splitlines()
                  if any(marker in line for marker in ("E2E ERROR", "[TIMEOUT]", "(FAIL)"))]
        geometry = 0
        if name == "reading_navigation_boundary" and result.returncode == 0 and not errors:
            geometry = subprocess.run([sys.executable, str(ROOT / "tests/check_navigation_evidence.py"),
                                       str(directory)], cwd=ROOT).returncode
        if name == "improvement_21_file_picker" and result.returncode == 0 and not errors:
            layout = json.loads((directory / "improvement_21_results.json").read_text())
            geometry = int(not any(n.get("name") == "file_picker_result"
                                   and n.get("focus_target", {}).get("item") == "src/app.cpp"
                                   and n.get("text", "").startswith("app.cpp")
                                   and n["visible_rect"]["width"] > 100 and n["visible_rect"]["height"] > 0
                                   for n in layout["nodes"]))
        if name == "improvement_27_commit_search" and result.returncode == 0 and not errors:
            layout = json.loads((directory / "improvement_27_open.json").read_text())
            geometry = int(not any(n.get("name") == "file_header_label" and n.get("text") == "tests/test_utils.cpp"
                                   and n["visible_rect"]["width"] > 100 and n["visible_rect"]["height"] > 0
                                   for n in layout["nodes"]))
        passed = result.returncode == 0 and not errors and geometry == 0
        results.append(dict(script=name, passed=passed, exit_code=result.returncode,
                            geometry_exit_code=geometry, errors=errors))
        print(f"{'PASS' if passed else 'FAIL'} {name}", flush=True)
        if errors:
            print("\n".join(errors[:10]), flush=True)
    binary_unchanged = binary_hash == hashlib.sha256(binary.read_bytes()).hexdigest()
    report = dict(binary_sha256=binary_hash, binary_unchanged=binary_unchanged, results=results)
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    if not binary_unchanged or not all(row["passed"] for row in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
