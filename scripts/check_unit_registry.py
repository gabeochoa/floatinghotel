#!/usr/bin/env python3
"""One registry for unit tests: tests/run_unit_tests.sh.

Drift evidence: makefile TEST_EXES listed 16 suites, the shell runner 53,
tests/unit held 57 files; test_git_commands existed only in the makefile
(and its makefile rule was missing git_parser.cpp, breaking its link),
test_outline in neither. A suite in no registry is a test that never runs.
This check fails if any tests/unit/test_*.cpp is absent from the runner,
or if the makefile's TEST_EXES names a suite the runner does not know.
The makefile `test` target delegates to the runner — do not add a second list.
"""
import pathlib, re, sys
files={p.stem for p in pathlib.Path("tests/unit").glob("test_*.cpp")}
runner=pathlib.Path("tests/run_unit_tests.sh").read_text()
in_runner={m for m in files if f'"{m}"' in runner or f"'{m}'" in runner}
make=pathlib.Path("makefile").read_text()
block=re.search(r"TEST_EXES\s*:=(.*?)\n\n", make, re.S)
in_make=set(re.findall(r"test_\w+", block.group(1))) if block else set()
bad=[f"in tests/unit but not run_unit_tests.sh: {x}" for x in sorted(files-in_runner)]
bad+=[f"in makefile TEST_EXES but not runner: {x}" for x in sorted(in_make-in_runner)]
if bad: print("FAIL unit registry drift:"); print("\n".join(bad)); sys.exit(1)
print(f"ok unit registry: {len(files)} suites, runner is sole registry")
