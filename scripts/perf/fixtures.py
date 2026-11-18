#!/usr/bin/env python3
"""Deterministic perf fixtures: small / medium / large repos via fast-import.

Usage: scripts/perf/fixtures.py [--output DIR] [--force] [small|medium|large ...]
Prints one line per fixture: name, path, tree hash, seconds to build.
Identical parameters rebuild to identical tree hashes (fixed authors,
dates, and contents). A fixture whose signature file matches is reused.
The generating git is the one on PATH (or $FH_GIT); its path and version
are printed so a run names which git ran.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

SIZES = {
    # files, commits, lines per file
    "small": (20, 10, 30),
    "medium": (300, 60, 60),
    "large": (2000, 150, 80),
}
EPOCH = 1_700_000_000


def git_binary():
    override = os.environ.get("FH_GIT")
    return override if override else shutil.which("git")


def stream_for(files, commits, lines):
    out = bytearray()
    paths = [f"src/mod{i % 25}/file{i:04}.cpp" for i in range(files)]
    for c in range(commits):
        title = f"Fixture commit {c}\n".encode()
        stamp = EPOCH + c * 60
        out.extend(f"commit refs/heads/main\nmark :{c + 1}\n".encode())
        out.extend(f"author Fixture <fixture@example.invalid> {stamp} +0000\n".encode())
        out.extend(f"committer Fixture <fixture@example.invalid> {stamp} +0000\n".encode())
        out.extend(f"data {len(title)}".encode() + b"\n" + title)
        if c:
            out.extend(f"from :{c}\n".encode())
        else:
            for i, path in enumerate(paths):
                body = "".join(f"int v{i}_{k} = {k};\n" for k in range(lines)).encode()
                out.extend(f"M 100644 inline {path}\ndata {len(body)}\n".encode() + body)
        # Every commit touches a deterministic slice of files.
        for i in range(c % 7 + 1):
            path = paths[(c * 13 + i * 37) % files]
            body = (f"// revision {c}\n" + "".join(f"int v_{(c * 13 + i * 37) % files}_{k} = {k + c};\n" for k in range(lines))).encode()
            out.extend(f"M 100644 inline {path}\ndata {len(body)}\n".encode() + body)
        out.extend(b"\n")
    return bytes(out)


def build(name, root, git):
    files, commits, lines = SIZES[name]
    signature = f"{name}:{files}:{commits}:{lines}:v1"
    repo = root / name
    marker = repo / ".fixture-signature"
    if marker.exists() and marker.read_text().strip() == signature:
        tree = subprocess.check_output([git, "-C", str(repo), "rev-parse", "HEAD^{tree}"], text=True).strip()
        print(f"{name} cached {repo} tree={tree}")
        return tree
    if repo.exists():
        shutil.rmtree(repo)
    repo.mkdir(parents=True)
    subprocess.run([git, "-C", str(repo), "init", "-q", "-b", "main"], check=True)
    started = time.time()
    subprocess.run([git, "-C", str(repo), "fast-import", "--quiet"], input=stream_for(files, commits, lines), check=True)
    subprocess.run([git, "-C", str(repo), "reset", "--hard", "-q", "HEAD"], check=True)
    seconds = time.time() - started
    tree = subprocess.check_output([git, "-C", str(repo), "rev-parse", "HEAD^{tree}"], text=True).strip()
    marker.write_text(signature + "\n")
    print(f"{name} built {repo} tree={tree} in {seconds:.2f}s")
    return tree


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("output/perf/fixtures"))
    parser.add_argument("--force", action="store_true")
    parser.add_argument("sizes", nargs="*", default=list(SIZES))
    args = parser.parse_args()
    git = git_binary()
    version = subprocess.check_output([git, "--version"], text=True).strip()
    print(f"git: {git} ({version})")
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if args.force:
        for name in args.sizes:
            target = root / name
            if target.exists():
                shutil.rmtree(target)
    for name in args.sizes:
        if name not in SIZES:
            print(f"unknown size: {name}", file=sys.stderr)
            return 2
        build(name, root, git)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
