#!/usr/bin/env python3
"""Run perf scenarios against the fixtures and summarise the reports.

Usage: scripts/perf/run.py [--runs N] [--warmups N] [--size small|medium|large]
Requires output/floatinghotel.exe. Each scenario runs `--warmups` unmeasured
times, then `--runs` measured times (7 by default); the app writes
output/perf/<scenario>.json via perf_report each time. Prints median/p95 of
the marks and the git spawn totals. Gates against tests/perf/baseline.json:
a git-run count above baseline fails, and a median mark slower than the
baseline by more than max(15%, 20 ms) fails (time is load-sensitive, so the
git count is the exact gate and time is the regression tripwire).
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = {"cold_start": "commits_visible", "quick_open": "quick_open_results",
             "warm_refresh": "warm_refresh_done", "resize_sweep": "resized"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--size", default="small")
    args = parser.parse_args()
    exe = ROOT / "output/floatinghotel.exe"
    if not exe.exists():
        print("build first: zig build", file=sys.stderr)
        return 2
    subprocess.run([sys.executable, str(ROOT / "scripts/perf/fixtures.py"), args.size],
                   check=True, cwd=ROOT, capture_output=True)
    fixture = ROOT / f"output/perf/fixtures/{args.size}"
    summaries = {}
    for scenario, mark in SCENARIOS.items():
        def run_once():
            subprocess.run([str(exe), str(fixture), "--test-mode", "--headless",
                            f"--test-script=tests/perf/{scenario}.e2e", "--e2e-timeout=60"],
                           check=True, cwd=ROOT, capture_output=True)
            return json.loads((ROOT / f"output/perf/{scenario}.json").read_text())

        for _ in range(args.warmups):
            run_once()
        reports = [run_once() for _ in range(args.runs)]
        marks = sorted(r["marks"][mark] for r in reports)
        summaries[scenario] = {
            "size": args.size, "runs": len(reports), "warmups": args.warmups,
            "mark_ms_median": statistics.median(marks),
            "mark_ms_p95": marks[min(len(marks) - 1, int(len(marks) * 0.95))],
            "git_runs_median": statistics.median([r["git"]["runs"] for r in reports]),
            "git": reports[-1]["git"],
        }
    print(json.dumps(summaries, indent=2))
    baseline_path = ROOT / "tests/perf/baseline.json"
    if baseline_path.exists():
        baseline = json.loads(baseline_path.read_text())
        if summaries["cold_start"]["git_runs_median"] > baseline.get("git_runs", float("inf")):
            print("FAIL: git spawn count increased over baseline", file=sys.stderr)
            return 1
        baselines = baseline.get("marks_ms_median", {})
        for scenario, summary in summaries.items():
            base = baselines.get(scenario)
            if base is None:
                continue
            limit = base * 1.15 + 20.0
            if summary["mark_ms_median"] > limit:
                print(f"FAIL: {scenario} median {summary['mark_ms_median']:.1f} ms "
                      f"exceeds baseline {base:.1f} ms by more than max(15%, 20 ms)",
                      file=sys.stderr)
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
